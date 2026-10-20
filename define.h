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

/* unified result codes */
enum result {
	OK = 0,
	ERR_IO,
	ERR_MAIL_INVALID,
	ERR_ADDR_INVALID,
	ERR_NOT_DELIVERABLE,
	ERR_NOT_EXIST,
	ERR_EXISTS,
};

/* a parsed mail */
struct mail {
	char   user[32];
	char   action[32];
	char   address[512];
	u32    date;    /* yyyymmdd */
	u16    time;    /* hhmm */
	char **content; /* body lines */
	u32    lines;
};

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

/* main.c */
int         start_daemon(const char *inbox, const char *archive);
enum result deliver(const char *archive, const struct mail *m);
enum result action_to(const char *archive, const struct mail *m);
enum result action_replace(const char *archive, const struct mail *m);
enum result action_delete(const char *archive, const struct mail *m);

/* logging */
void log_info(const char *fmt, ...);
void log_ok(const char *user, const char *action, const char *addr,
	    const char *result);
void log_err(const char *user, const char *action, const char *addr,
	     const char *err);
void log_now(char *buf, size_t n);
const char *result_str(enum result r);

#endif /* DEFINE_H */
