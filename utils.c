/*
 * pman - utility functions (pure functions / IO primitives)
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include "define.h"

/* Return non-zero if path exists. */
int is_exist(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0;
}

/* Return non-zero if path is a directory. */
int is_dir(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Return non-zero if path is a regular file. */
int is_file(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Return non-zero if addr is a valid mailbox address. */
int addr_valid(const char *addr)
{
	const char *seg, *p, *q;
	char c;

	if (!addr || !*addr || *addr == ':')
		return 0;

	seg = addr;
	for (p = addr;; p++) {
		if (*p == ':' || *p == '\0') {
			if (p == seg)
				return 0;
			for (q = seg; q < p; q++) {
				c = *q;
				if (!((c >= 'a' && c <= 'z') ||
				      (c >= 'A' && c <= 'Z') ||
				      (c >= '0' && c <= '9') ||
				      c == '_' || c == '-'))
					return 0;
			}
			if (*p == '\0')
				break;
			seg = p + 1;
		}
	}
	return 1;
}

/* Trim surrounding whitespace in place. Return the trimmed start. */
char *str_trim(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
		s++;
	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
			   end[-1] == '\r' || end[-1] == '\n'))
		end--;
	*end = '\0';
	return s;
}

/* Return a heap copy of s, or NULL on allocation failure. */
char *str_dup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);

	if (!p)
		return NULL;
	memcpy(p, s, n);
	return p;
}

/* Return "<dir>/<name>", or NULL on allocation failure. */
char *path_join(const char *dir, const char *name)
{
	size_t n = strlen(dir) + 1 + strlen(name) + 1;
	char *p = malloc(n);

	if (!p)
		return NULL;
	snprintf(p, n, "%s/%s", dir, name);
	return p;
}

/* Return non-zero if s is a non-empty hex string. */
int hex_is(const char *s)
{
	if (!*s)
		return 0;
	for (; *s; s++) {
		if (!((*s >= '0' && *s <= '9') ||
		      (*s >= 'a' && *s <= 'f') ||
		      (*s >= 'A' && *s <= 'F')))
			return 0;
	}
	return 1;
}

/* Return the hex value of s, or -1 if s is not pure hex. */
long hex_parse(const char *s)
{
	long v = 0;

	for (; *s; s++) {
		int d;

		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (*s >= 'a' && *s <= 'f')
			d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'F')
			d = *s - 'A' + 10;
		else
			return -1;
		v = v * 16 + d;
	}
	return v;
}

/* Return the largest sequence among addr's direct children, or -1. */
long seq_max(const char *archive, const char *addr)
{
	DIR *d = opendir(archive);
	size_t plen;
	char *prefix;
	struct dirent *e;
	long max = -1;

	if (!d)
		return -1;

	plen = strlen(addr);
	prefix = malloc(plen + 2);
	if (!prefix) {
		closedir(d);
		return -1;
	}
	memcpy(prefix, addr, plen);
	prefix[plen] = ':';
	prefix[plen + 1] = '\0';

	while ((e = readdir(d))) {
		const char *name = e->d_name;
		const char *suffix;
		char *full;
		long v;

		if (name[0] == '.')
			continue;
		if (strncmp(name, prefix, plen + 1) != 0)
			continue;
		suffix = name + plen + 1;
		if (strchr(suffix, ':'))	/* deeper child, not a direct seq */
			continue;
		if (!hex_is(suffix))
			continue;
		full = path_join(archive, name);
		if (!full)
			continue;
		if (!is_dir(full)) {
			free(full);
			continue;
		}
		free(full);
		v = hex_parse(suffix);
		if (v > max)
			max = v;
	}

	free(prefix);
	closedir(d);
	return max;
}

/* Parse a mail file into out. Return OK or an error code. */
enum result mail_parse(const char *path, struct mail *out)
{
	FILE *f = fopen(path, "rb");
	char *line = NULL;
	size_t cap = 0;
	int lineno = 0;
	char *from_line = NULL, *date_line = NULL;
	char **content = NULL;
	u32 lines = 0, lines_cap = 0;
	enum result rc = ERR_MAIL_INVALID;

	if (!f)
		return ERR_IO;

	while (getline(&line, &cap, f) != -1) {
		lineno++;
		if (lineno == 1) {
			from_line = str_dup(line);
		} else if (lineno == 2) {
			date_line = str_dup(line);
		} else if (lineno == 3) {
			if (*str_trim(line) != '\0')	/* third line must be blank */
				goto done;
		} else {
			if (lines == lines_cap) {
				lines_cap = lines_cap ? lines_cap * 2 : 8;
				char **nb = realloc(content,
						     lines_cap * sizeof(*nb));
				if (!nb)
					goto done;
				content = nb;
			}
			content[lines] = str_dup(line);
			if (!content[lines])
				goto done;
			lines++;
		}
	}
	if (lineno < 2)
		goto done;

	memset(out, 0, sizeof(*out));

	/* "from: <user> <action> <address>" */
	{
		char *fl = str_trim(from_line);
		char *rest, *sp, *rest2;

		if (strncmp(fl, "from:", 5) != 0)
			goto done;
		rest = str_trim(fl + 5);
		sp = strchr(rest, ' ');
		if (!sp)
			goto done;
		*sp = '\0';
		if (*rest == '\0')
			goto done;
		strncpy(out->user, rest, sizeof(out->user) - 1);
		rest2 = str_trim(sp + 1);
		sp = strchr(rest2, ' ');
		if (!sp)
			goto done;
		*sp = '\0';
		strncpy(out->action, rest2, sizeof(out->action) - 1);
		strncpy(out->address, str_trim(sp + 1),
			sizeof(out->address) - 1);
	}

	/* "date: YYYY-MM-DD HH:MM" */
	{
		u32 y, mo, d, h, mi;
		char *dl = str_trim(date_line);

		if (strncmp(dl, "date:", 5) != 0)
			goto done;
		if (sscanf(dl + 5, "%u-%u-%u %u:%u", &y, &mo, &d, &h, &mi) != 5)
			goto done;
		out->date = y * 10000 + mo * 100 + d;
		out->time = (u16)(h * 100 + mi);
	}

	if (strcmp(out->action, "to") != 0 &&
	    strcmp(out->action, "replace") != 0 &&
	    strcmp(out->action, "delete") != 0)
		goto done;
	if (!addr_valid(out->address))
		goto done;

	out->content = content;
	out->lines = lines;
	content = NULL;
	rc = OK;

done:
	free(line);
	free(from_line);
	free(date_line);
	if (content) {
		u32 i;

		for (i = 0; i < lines; i++)
			free(content[i]);
		free(content);
	}
	fclose(f);
	return rc;
}

/* Return non-zero if path holds a valid mail. */
int is_valid_mail(const char *path)
{
	struct mail m;
	enum result r = mail_parse(path, &m);

	if (r == OK)
		mail_free(&m);
	return r == OK;
}

/* Free memory owned by m. */
void mail_free(struct mail *m)
{
	if (m->content) {
		u32 i;

		for (i = 0; i < m->lines; i++)
			free(m->content[i]);
		free(m->content);
	}
	memset(m, 0, sizeof(*m));
}

/* Write data to path. Return OK or ERR_IO. */
enum result file_write(const char *path, const char *data)
{
	FILE *f = fopen(path, "wb");
	size_t n;
	enum result rc = OK;

	if (!f)
		return ERR_IO;
	n = strlen(data);
	if (n > 0 && fwrite(data, 1, n, f) != n)
		rc = ERR_IO;
	if (fclose(f) != 0)
		rc = ERR_IO;
	return rc;
}

/* Recursively remove path. Return OK or ERR_IO. */
enum result dir_rm_rf(const char *path)
{
	struct stat st;

	if (lstat(path, &st) != 0)
		return ERR_IO;

	if (S_ISDIR(st.st_mode)) {
		DIR *d = opendir(path);
		struct dirent *e;
		enum result rc = OK;

		if (!d)
			return ERR_IO;
		while ((e = readdir(d))) {
			char *child;

			if (strcmp(e->d_name, ".") == 0 ||
			    strcmp(e->d_name, "..") == 0)
				continue;
			child = path_join(path, e->d_name);
			if (!child || dir_rm_rf(child) != OK)
				rc = ERR_IO;
			free(child);
		}
		closedir(d);
		if (rmdir(path) != 0)
			rc = ERR_IO;
		return rc;
	}
	return unlink(path) == 0 ? OK : ERR_IO;
}

/* Create path as a directory. An existing directory is OK. */
enum result dir_make(const char *path)
{
	if (mkdir(path, 0755) == 0 || errno == EEXIST)
		return OK;
	return ERR_IO;
}

/* Move src to dst, appending a timestamp if dst already exists. */
enum result file_move(const char *src, const char *dst)
{
	char tmp[PATH_MAX];

	if (rename(src, dst) == 0)
		return OK;
	snprintf(tmp, sizeof(tmp), "%s.%ld", dst, (long)time(NULL));
	if (rename(src, tmp) == 0)
		return OK;
	return ERR_IO;
}

/* Move/rename a directory. Return OK, ERR_EXISTS or ERR_IO. */
enum result dir_move(const char *old_path, const char *new_path)
{
	if (rename(old_path, new_path) == 0)
		return OK;
	if (errno == ENOTEMPTY || errno == EEXIST)
		return ERR_EXISTS;
	return ERR_IO;
}
