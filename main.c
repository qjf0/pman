/* pman @alpha 0.5
 * https://github.com/qjf0/pman
 *
 * Maintainer: qjf0 <qianjunfan0@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>

#include "config.h"
#include "define.h"

/* ANSI colors */
#define RESET  "\033[0m"
#define DIM    "\033[2m"
#define RED    "\033[31m"
#define GREEN  "\033[32m"
#define CYAN   "\033[36m"

/* log levels */
#define LOG_INFO 0
#define LOG_OK   1
#define LOG_ERR  2

/* global lookup tables */
static const char *log_tags[]   = { "INFO", "OK  ", "ERR " };
static const char *log_colors[] = { CYAN, GREEN, RED };

/* Print one log line; msg is already formatted. */
static void log_print(int lv, const char *msg)
{
	char t[16];

	log_now(t, sizeof(t));
	fprintf(stdout, "%s[%s]%s %s%s%s %s\n",
		DIM, t, RESET, log_colors[lv], log_tags[lv], RESET, msg);
}

/* info: free-form message */
void log_info(const char *fmt, ...)
{
	char msg[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	log_print(LOG_INFO, msg);
}

/* ok: user action address [-> result] */
void log_ok(const char *user, const char *action, const char *addr,
	    const char *result)
{
	char msg[1024];

	if (result)
		snprintf(msg, sizeof(msg), "%-8s%-8s%s -> %s",
			 user, action, addr, result);
	else
		snprintf(msg, sizeof(msg), "%-8s%-8s%s", user, action, addr);
	log_print(LOG_OK, msg);
}

/* err: user action address error */
void log_err(const char *user, const char *action, const char *addr,
	     const char *err)
{
	char msg[1024];

	snprintf(msg, sizeof(msg), "%-8s%-8s%-12s%s", user, action, addr, err);
	log_print(LOG_ERR, msg);
}

/* qsort comparator for file names. */
static int str_cmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Collect *.mail files from inbox, sorted by name. Returns the count,
 * or -1 if inbox cannot be opened. */
static int collect_mails(const char *inbox, char ***out)
{
	DIR *d = opendir(inbox);
	struct dirent *e;
	char **mails = NULL;
	size_t n = 0, cap = 0;

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);

		if (len < 5 || strcmp(e->d_name + len - 5, ".mail") != 0)
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 16;
			mails = realloc(mails, cap * sizeof(*mails));
			if (!mails)
				exit(1);
		}
		mails[n++] = str_dup(e->d_name);
	}
	closedir(d);

	qsort(mails, n, sizeof(*mails), str_cmp);
	*out = mails;
	return (int)n;
}

/* Archive file name: user_action_address_<unix timestamp>.mail */
static char *archive_name(const struct mail *m)
{
	char name[1024];

	snprintf(name, sizeof(name), "%s_%s_%s_%ld.mail",
		 m->user, m->action, m->address, (long)time(NULL));
	return str_dup(name);
}

/* Parse arguments, process every .mail, then exit. */
int main(int argc, char *argv[])
{
	char **mails = NULL;
	int n, i;

	if (argc != 3) {
		fprintf(stderr, "usage: %s inbox archive\n", argv[0]);
		return 1;
	}

	n = collect_mails(argv[1], &mails);
	if (n < 0) {
		fprintf(stderr, "cannot open inbox: %s\n", argv[1]);
		return 1;
	}
	for (i = 0; i < n; i++) {
		process_mail(argv[1], argv[2], mails[i]);
		free(mails[i]);
	}
	free(mails);
	return 0;
}

/* main functions, implemented below main() */

/* Return non-zero if addr is deliverable (declared or generated). */
static int deliverable(const char *archive, const char *addr)
{
	int i;
	char *full;
	int d;

	for (i = 0; declares[i]; i++)
		if (strcmp(declares[i], addr) == 0)
			return 1;

	full = path_join(archive, addr);
	if (!full)
		return 0;
	d = is_dir(full);
	free(full);
	return d;
}

/* Write the six field files into dir. Return OK or error. */
static enum result write_fields(const char *dir, const struct mail *m)
{
	char *user_path = path_join(dir, "user");
	char *action_path = path_join(dir, "action");
	char *address_path = path_join(dir, "address");
	char *date_path = path_join(dir, "date");
	char *time_path = path_join(dir, "time");
	char *content_path = path_join(dir, "content");
	char date_content[16];
	char time_content[8];
	char *body;
	size_t total = 0, off = 0;
	u32 y, mo, d, h, mi, i;
	enum result rc = OK;

	if (!user_path || !action_path || !address_path ||
	    !date_path || !time_path || !content_path) {
		free(user_path);
		free(action_path);
		free(address_path);
		free(date_path);
		free(time_path);
		free(content_path);
		return ERR_IO;
	}

	/* date: "YYYY-MM-DD"; time: "HH:MM" */
	y = m->date / 10000;
	mo = (m->date / 100) % 100;
	d = m->date % 100;
	h = m->time / 100;
	mi = m->time % 100;
	snprintf(date_content, sizeof(date_content), "%04u-%02u-%02u",
		 y, mo, d);
	snprintf(time_content, sizeof(time_content), "%02u:%02u", h, mi);

	/* content: concatenated body lines */
	for (i = 0; i < m->lines; i++)
		total += strlen(m->content[i]);
	body = malloc(total + 1);
	if (!body) {
		free(user_path);
		free(action_path);
		free(address_path);
		free(date_path);
		free(time_path);
		free(content_path);
		return ERR_IO;
	}
	for (i = 0; i < m->lines; i++) {
		size_t l = strlen(m->content[i]);

		memcpy(body + off, m->content[i], l);
		off += l;
	}
	body[off] = '\0';

	if (file_write(user_path, m->user) != OK ||
	    file_write(action_path, m->action) != OK ||
	    file_write(address_path, m->address) != OK ||
	    file_write(date_path, date_content) != OK ||
	    file_write(time_path, time_content) != OK ||
	    file_write(content_path, body) != OK)
		rc = ERR_IO;

	free(user_path);
	free(action_path);
	free(address_path);
	free(date_path);
	free(time_path);
	free(content_path);
	free(body);
	return rc;
}

/* Atomically create a new box directory and write its fields.
 * Writes into a .tmp.<name>.<timestamp> directory, then moves it into
 * place. Returns OK, ERR_EXISTS (target taken) or ERR_IO. */
static enum result create_box(const char *archive, const char *name,
			      const struct mail *m)
{
	char tmpname[1024];
	char *tmpdir, *final_dir;
	enum result rc;

	snprintf(tmpname, sizeof(tmpname), ".tmp.%s.%ld",
		 name, (long)time(NULL));
	tmpdir = path_join(archive, tmpname);
	final_dir = path_join(archive, name);
	if (!tmpdir || !final_dir) {
		free(tmpdir);
		free(final_dir);
		return ERR_IO;
	}

	if (dir_make(tmpdir) != OK) {
		free(tmpdir);
		free(final_dir);
		return ERR_IO;
	}

	if (write_fields(tmpdir, m) != OK) {
		dir_rm_rf(tmpdir);
		free(tmpdir);
		free(final_dir);
		return ERR_IO;
	}

	rc = dir_move(tmpdir, final_dir);
	if (rc != OK)
		dir_rm_rf(tmpdir);
	free(tmpdir);
	free(final_dir);
	return rc;
}

/* Create a numbered child of m->address and write the mail. */
enum result action_to(const char *archive, const struct mail *m)
{
	long seq;

	if (!deliverable(archive, m->address))
		return ERR_NOT_DELIVERABLE;

	seq = seq_max(archive, m->address);
	for (;;) {
		char hexbuf[64];
		char newaddr[600];
		enum result r;

		seq++;
		snprintf(hexbuf, sizeof(hexbuf), "%lx", seq);
		snprintf(newaddr, sizeof(newaddr), "%s:%s", m->address, hexbuf);

		r = create_box(archive, newaddr, m);
		if (r == OK) {
			log_ok(m->user, "to", m->address, newaddr);
			return OK;
		}
		if (r == ERR_EXISTS)
			continue;	/* sequence taken, try the next one */
		return r;
	}
}

/* Overwrite an existing box, keeping its children. */
enum result action_replace(const char *archive, const struct mail *m)
{
	char *dir = path_join(archive, m->address);
	enum result r;

	if (!dir)
		return ERR_IO;
	if (!is_dir(dir)) {
		free(dir);
		return ERR_NOT_EXIST;
	}
	r = write_fields(dir, m);
	if (r == OK)
		log_ok(m->user, "replace", m->address, NULL);
	free(dir);
	return r;
}

/* Remove an existing box and its subtree. Body is ignored. */
enum result action_delete(const char *archive, const struct mail *m)
{
	char *dir = path_join(archive, m->address);
	enum result r;

	if (!dir)
		return ERR_IO;
	if (!is_dir(dir)) {
		free(dir);
		return ERR_NOT_EXIST;
	}
	r = dir_rm_rf(dir);
	if (r == OK)
		log_ok(m->user, "delete", m->address, NULL);
	free(dir);
	return r;
}

/* Deliver a parsed mail by dispatching to its action. Return OK or error. */
enum result deliver(const char *archive, const struct mail *m)
{
	if (strcmp(m->action, "to") == 0)
		return action_to(archive, m);
	if (strcmp(m->action, "replace") == 0)
		return action_replace(archive, m);
	if (strcmp(m->action, "delete") == 0)
		return action_delete(archive, m);
	return ERR_MAIL_INVALID;
}

/* Process one .mail file: parse, deliver, archive into succeed/ or failed/. */
void process_mail(const char *inbox, const char *archive, const char *name)
{
	char *src = path_join(inbox, name);
	struct mail m;
	enum result r;
	const char *box;
	char *dir, *dst, *aname;

	if (!src)
		return;

	r = parse_mail(src, &m);
	if (r == OK) {
		r = deliver(archive, &m);
		if (r != OK)
			log_err(m.user, m.action, m.address, result_str(r));
		aname = archive_name(&m);
		mail_free(&m);
	} else {
		log_err("-", "-", name, result_str(r));
		aname = str_dup(name);
	}
	box = (r == OK) ? "succeed" : "failed";

	dir = path_join(inbox, box);
	if (!dir) {
		free(src);
		free(aname);
		return;
	}
	dir_make(dir);
	dst = path_join(dir, aname);
	if (dst) {
		file_move(src, dst);
		free(dst);
	}
	free(dir);
	free(src);
	free(aname);
}
