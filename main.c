/* pman @alpha 0.3
 * https://github.com/qjf0/pman
 *
 * Maintainer: qjf0 <qianjunfan0@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "config.h"
#include "define.h"

/* signal flag */
static volatile sig_atomic_t g_stop = 0;

/* Record the stop request from a signal. */
static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

/* Sleep for ms milliseconds. */
static void sleep_ms(long ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}

/* Parse arguments and run the monitor. Return exit code. */
int main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s inbox archive\n", argv[0]);
		return 1;
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	return start_daemon(argv[1], argv[2]);
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
			printf("to: %s -> %s (from %s)\n",
			       m->address, newaddr, m->user);
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
		printf("replace: %s (from %s)\n", m->address, m->user);
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
		printf("delete: %s (from %s)\n", m->address, m->user);
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

/* Process one source mail and archive it into savebox/ or failed/. */
static void handle_file(const char *inbox, const char *archive,
			const char *name)
{
	char *src = path_join(inbox, name);
	struct mail m;
	enum result r;
	const char *box;
	char *dir, *dst;

	if (!src)
		return;

	/* parse first, checking syntax along the way */
	r = parse_mail(src, &m);
	if (r == OK) {
		/* then deliver */
		r = deliver(archive, &m);
		mail_free(&m);
	}
	box = (r == OK) ? "savebox" : "failed";

	dir = path_join(inbox, box);
	if (!dir) {
		free(src);
		return;
	}
	dir_make(dir);
	dst = path_join(dir, name);
	if (dst) {
		file_move(src, dst);
		free(dst);
	}
	free(dir);
	free(src);
}

/* Run the foreground monitor loop. Return the exit code. */
int start_daemon(const char *inbox, const char *archive)
{
	long stable_sec = stable_ms / 1000;

	while (!g_stop) {
		DIR *d = opendir(inbox);
		struct dirent *e;
		time_t now = time(NULL);

		if (!d) {
			fprintf(stderr, "cannot open inbox: %s\n", inbox);
			sleep_ms(1000);
			continue;
		}

		while ((e = readdir(d))) {
			const char *name = e->d_name;
			char *full;
			struct stat st;
			int ready = 0;

			if (name[0] == '.')
				continue;
			full = path_join(inbox, name);
			if (!full)
				continue;
			if (stat(full, &st) == 0 && S_ISREG(st.st_mode) &&
			    now - st.st_mtime >= stable_sec)
				ready = 1;
			free(full);
			if (ready)
				handle_file(inbox, archive, name);
		}
		closedir(d);

		sleep_ms(poll_ms);
	}
	return 0;
}
