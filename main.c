/* pman @alpha 0.8
 * https://github.com/qjf0/pman
 *
 * Maintainer: qjf0 <qianjunfan0@outlook.com>
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"

#define VERSION  "alpha 0.7"
#define PAIRCODE "0"
#define SIZE     256 * 1024

#define ERR_PAIRCODE_MISMATCH 1
#define ERR_USER_NOT_FOUND    2
#define ERR_WRONG_PASSWORD    3
#define ERR_NO_PERMISSION     4
#define ERR_DB_IO             5
#define ERR_UNKNOWN_ACTION    6

static struct {
        char paircode[SIZE];
        char str_buf[SIZE];

        char file_path[SIZE];
        size_t file_size;
        char file[SIZE];
        char db_path[SIZE];

        char user[SIZE];
        char passwd[SIZE];

        char action[SIZE];
        char address[SIZE];
        char title[SIZE];
        char content[SIZE];

        /* YYYYMMDDHHMMSS */
        size_t time_utc;
} rt;

static void load(void)
{
        FILE *fp = fopen(rt.file_path, "r");

        rt.file_size = fread(rt.file, 1, sizeof(rt.file) - 1, fp);
        rt.file[rt.file_size] = '\0';
        fclose(fp);
}

static void parse(void)
{
        char *p = rt.file;
        char *end;

        end = strchr(p, '\n');
        *end = '\0';
        strcpy(rt.paircode, p);
        p = end + 1;

        end = strchr(p, '\n');
        *end = '\0';
        strcpy(rt.action, p);
        p = end + 1;

        end = strchr(p, '\n');
        *end = '\0';
        strcpy(rt.address, p);
        p = end + 1;

        end = strchr(p, '\n');
        *end = '\0';
        strcpy(rt.title, p);
        p = end + 1;

        strcpy(rt.content, p);
}

/* Current UTC time in YYYYMMDDHHMMSS format. */
static void get_time(void)
{
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);

        rt.time_utc = (size_t)(tm->tm_year + 1900) * 10000000000
                    + (size_t)(tm->tm_mon + 1) * 100000000
                    + (size_t)tm->tm_mday * 1000000
                    + (size_t)tm->tm_hour * 10000
                    + (size_t)tm->tm_min * 100
                    + (size_t)tm->tm_sec;
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* '*' matches any character (including ':'); all other
 * characters are compared byte by byte. */
static int pattern_match(const char *pat, size_t plen, const char *str)
{
        size_t pi = 0;
        size_t si = 0;
        size_t star = (size_t)-1;
        size_t retry = 0;

        while (str[si] != '\0') {
                if (pi < plen && pat[pi] == '*') {
                        star = pi++;
                        retry = si;
                } else if (pi < plen && pat[pi] == str[si]) {
                        pi++;
                        si++;
                } else if (star != (size_t)-1) {
                        pi = star + 1;
                        si = ++retry;
                } else {
                        return 0;
                }
        }

        while (pi < plen && pat[pi] == '*')
                pi++;

        return pi == plen;
}

/* allowed is a whitespace-separated rule list; any matching rule is enough. */
static int allowed_match(const char *allowed, const char *address)
{
        const char *p = allowed;

        while (*p != '\0') {
                size_t len;

                while (*p == ' ' || *p == '\t' ||
                       *p == '\n' || *p == '\r')
                        p++;
                if (*p == '\0')
                        break;

                len = strcspn(p, " \t\n\r");
                if (pattern_match(p, len, address))
                        return 1;
                p += len;
        }

        return 0;
}

/* Returns 0 when verification succeeds. */
static int verify(void)
{
        size_t i;

        if (strcmp(rt.paircode, PAIRCODE) != 0)
                return ERR_PAIRCODE_MISMATCH;

        for (i = 0; i < ARRAY_SIZE(user_config); i++) {
                if (strcmp(user_config[i].name, rt.user) != 0)
                        continue;
                if (strcmp(user_config[i].password, rt.passwd) != 0)
                        return ERR_WRONG_PASSWORD;
                if (!allowed_match(user_config[i].allowed, rt.address))
                        return ERR_NO_PERMISSION;
                return 0;
        }

        return ERR_USER_NOT_FOUND;
}

/* Scan db and return the largest sequence number among address's direct children. */
static long seq_max(const char *address)
{
        DIR *d = opendir(rt.db_path);
        struct dirent *e;
        size_t len = strlen(address);
        long max = -1;

        while ((e = readdir(d))) {
                char *suffix;
                long v;

                if (strncmp(e->d_name, address, len) != 0 ||
                    e->d_name[len] != ':')
                        continue;
                suffix = e->d_name + len + 1;
                if (strchr(suffix, ':'))
                        continue;
                v = strtol(suffix, NULL, 16);
                if (v > max)
                        max = v;
        }

        closedir(d);
        return max;
}

/* Write user, UTC time, action, address, title and content to db/<address>. */
static int write_mail(const char *address)
{
        char tmp_path[SIZE];
        char final_path[SIZE];
        FILE *fp;

        snprintf(tmp_path, sizeof(tmp_path), "%s/.tmp.%zu",
                 rt.db_path, rt.time_utc);
        snprintf(final_path, sizeof(final_path), "%s/%s",
                 rt.db_path, address);

        fp = fopen(tmp_path, "w");
        if (fp == NULL)
                return ERR_DB_IO;

        fprintf(fp, "%s\n%zu\n%s\n%s\n%s\n%s", rt.user, rt.time_utc,
                rt.action, address, rt.title, rt.content);
        fclose(fp);

        if (rename(tmp_path, final_path) != 0) {
                remove(tmp_path);
                return ERR_DB_IO;
        }

        return 0;
}

static int action_to(void)
{
        char address[SIZE];
        unsigned long seq = (unsigned long)seq_max(rt.address) + 1;

        snprintf(address, sizeof(address), "%s:%lx", rt.address, seq);
        return write_mail(address);
}

static int action_ow(void)
{
        return write_mail(rt.address);
}

static int deliver(void)
{
        if (strcmp(rt.action, "to") == 0)
                return action_to();
        if (strcmp(rt.action, "ow") == 0)
                return action_ow();
        return ERR_UNKNOWN_ACTION;
}

int main(int argc, char **argv)
{
        if (argc != 5) {
                fprintf(stderr, "usage: %s <mail> <db> <user> <passwd>\n",
                        argv[0]);
                return EXIT_FAILURE;
        }

        snprintf(rt.file_path, sizeof(rt.file_path), "%s", argv[1]);
        snprintf(rt.db_path, sizeof(rt.db_path), "%s", argv[2]);
        snprintf(rt.user, sizeof(rt.user), "%s", argv[3]);
        snprintf(rt.passwd, sizeof(rt.passwd), "%s", argv[4]);

        int result;

        load();
        parse();

        result = verify();
        if (result)
                return result;

        get_time();

        result = deliver();
        return result;
}
