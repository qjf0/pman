/* pman - shared types and declarations */
#ifndef DEFINE_H
#define DEFINE_H

#include <stdint.h>
#include <stddef.h>

/* integer aliases */
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* unified result codes (doubled as exit codes; 1 is the usage error) */
enum result {
	OK = 0,
	ERR_IO = 2,
	ERR_MAIL_INVALID = 3,
	ERR_ADDR_INVALID = 4,
	ERR_NOT_EXIST = 5,
	ERR_EXISTS = 6,
};

/* a parsed mail */
struct mail {
	char   user[32];
	char   action[32];
	char   address[512];
	u32    date;            /* yyyymmdd */
	u16    time;            /* hhmm */
	char **content;         /* body lines */
	u32    lines;
};

/* one entry in the deliver map */
struct action {
	const char *name;
	enum result (*fn)(const struct mail *m);
};

/* global runtime: all shared state lives here */
struct runtime {
	const char *archive;   /* archive dir (succeed/ failed/ parent) */
	const char *mail_path; /* the mail file to process */
	const char *db;        /* database = output dir (box storage) */
	struct mail mail;      /* currently parsed mail */
};

extern struct runtime rt;

/* utils.c */
int         is_exist(const char *path);
int         is_dir(const char *path);
int         is_file(const char *path);
int         addr_valid(const char *addr);
char       *str_trim(char *s);
char       *str_dup(const char *s);
char       *path_join(const char *dir, const char *name);
int         hex_is(const char *s);
long        hex_parse(const char *s);
long        seq_max(const char *archive, const char *addr);
enum result parse_mail(const char *path, struct mail *out);
void        mail_free(struct mail *m);
enum result file_write(const char *path, const char *data);
enum result dir_rm_rf(const char *path);
enum result dir_make(const char *path);
enum result file_move(const char *src, const char *dst);
enum result dir_move(const char *old_path, const char *new_path);
const char *base_name(const char *path);
char       *archive_name(const struct mail *m);

/* main.c */
enum result parse(void);
enum result deliver(const struct mail *m);
void        archive(enum result r, int parsed);
void        free_runtime(void);

/* action.c */
enum result action_to(const struct mail *m);
enum result action_replace(const struct mail *m);
enum result action_delete(const struct mail *m);

#endif /* DEFINE_H */
