/* pman - mail actions (to / replace / delete) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "define.h"

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

/* Atomically create a new box directory under rt.db and write its fields.
 * Returns OK, ERR_EXISTS (target taken) or ERR_IO. */
static enum result create_box(const char *name, const struct mail *m)
{
	char tmpname[1024];
	char *tmpdir, *final_dir;
	enum result rc;

	snprintf(tmpname, sizeof(tmpname), ".tmp.%s.%ld",
		 name, (long)time(NULL));
	tmpdir = path_join(rt.db, tmpname);
	final_dir = path_join(rt.db, name);
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
enum result action_to(const struct mail *m)
{
	long seq = seq_max(rt.db, m->address);

	for (;;) {
		char hexbuf[64];
		char newaddr[600];
		enum result r;

		seq++;
		snprintf(hexbuf, sizeof(hexbuf), "%lx", seq);
		snprintf(newaddr, sizeof(newaddr), "%s:%s", m->address, hexbuf);

		r = create_box(newaddr, m);
		if (r == OK)
			return OK;
		if (r == ERR_EXISTS)
			continue;	/* sequence taken, try the next one */
		return r;
	}
}

/* Overwrite an existing box, keeping its children. */
enum result action_replace(const struct mail *m)
{
	char *dir = path_join(rt.db, m->address);
	enum result r;

	if (!dir)
		return ERR_IO;
	if (!is_dir(dir)) {
		free(dir);
		return ERR_NOT_EXIST;
	}
	r = write_fields(dir, m);
	free(dir);
	return r;
}

/* Remove an existing box and its subtree. Body is ignored. */
enum result action_delete(const struct mail *m)
{
	char *dir = path_join(rt.db, m->address);
	enum result r;

	if (!dir)
		return ERR_IO;
	if (!is_dir(dir)) {
		free(dir);
		return ERR_NOT_EXIST;
	}
	r = dir_rm_rf(dir);
	free(dir);
	return r;
}
