/* pman @alpha 0.6
 * https://github.com/qjf0/pman
 *
 * Maintainer: qjf0 <qianjunfan0@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

struct runtime rt;

int main(int argc, char *argv[])
{
	enum result r;
	int parsed;

	if (argc != 4) {
		fprintf(stderr, "usage: %s archive mail db\n", argv[0]);
		return 1;
	}

	rt.archive = argv[1];
	rt.mail_path = argv[2];
	rt.db = argv[3];

	r = parse();

	parsed = (r == OK);
	if (parsed)
		r = deliver(&rt.mail);

	archive(r, parsed);

	free_runtime();

	return (int)r;
}

/* Parse .mail file into rt.mail. Return OK or an error code. */
enum result parse(void)
{
	return parse_mail(rt.mail_path, &rt.mail);
}

/* Dispatch m by its action through the deliver map. */
enum result deliver(const struct mail *m)
{
	int i;

	for (i = 0; deliver_map[i].name; i++)
		if (strcmp(deliver_map[i].name, m->action) == 0)
			return deliver_map[i].fn(m);
	return ERR_MAIL_INVALID;
}

/* Move the source mail into succeed/ or failed/ under rt.archive. */
void archive(enum result r, int parsed)
{
	const char *box = (r == OK) ? "succeed" : "failed";
	char *name = parsed ? archive_name(&rt.mail)
			     : (char *)base_name(rt.mail_path);
	char *dir = path_join(rt.archive, box);
	char *dst;

	if (!dir)
		return;
	dir_make(dir);
	dst = path_join(dir, name);
	if (dst) {
		file_move(rt.mail_path, dst);
		free(dst);
	}
	if (parsed)
		free(name);
	free(dir);
}

/* Release all resources held by rt. */
void free_runtime(void)
{
	mail_free(&rt.mail);
}
