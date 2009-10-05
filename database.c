/*
 * Copyright (c) 2004 Iwata <iratqq@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "hddtemp.h"

typedef enum {
	DB_COMMENT,
	DB_START,
	DB_MODEL_REGEXP,
	DB_MODEL_REGEXP_SPC,
	DB_ID,
	DB_ID_SPC,
	DB_UNIT,
	DB_UNIT_SPC,
	DB_MODEL,
	DB_END
} database_state;

hdd_database*
database_new()
{
	hdd_database *db;

	db = malloc(sizeof(hdd_database));
	memset(db, 0, sizeof(hdd_database));
	return db;
}

/*
 * dbparser is simple paser of hddtemp.db
 * this function is required tail-recursive optimization
 */
static int
dbparser_core(FILE *fp, char c, char *buf, int buflen,
	      hdd_database *db, hdd_database *tmpdb, database_state state, int lineno)
{
	const char *errstr;

	if (feof(fp))
		return 1;
	if (DBLINEBUFMAX < buflen) {
		fprintf(stderr, "%d: buffer full\n", lineno);
		return 0;
	}
	switch (c) {
	case '\n': /* end of line, append stored database to databse list */
		lineno++;
		switch (state) {
		case DB_COMMENT:
			/* reset comment */
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_START, lineno);
		case DB_START:
			/* empty line */
			memset(buf, 0, DBLINEBUFMAX);
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_START, lineno);
		case DB_END:
			/* append one database entry */
			db->next = tmpdb;
			db = tmpdb;
			memset(buf, 0, DBLINEBUFMAX);
			return dbparser_core(fp, fgetc(fp), buf, 0, db, database_new(), DB_START, lineno);
		default:
			fprintf(stderr, "%d: unexpected end of line\n", lineno);
			return 0;
		}
	case '#': /* comment */
		switch (state) {
		case DB_COMMENT:
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_COMMENT, lineno);
		case DB_START:
			return dbparser_core(fp, fgetc(fp), buf, 0, db, database_new(), DB_COMMENT, lineno);
		case DB_MODEL_REGEXP:
		case DB_MODEL:
			return dbparser_core(fp, fgetc(fp), buf, 0, db, database_new(), state, lineno);
		case DB_END:
			return dbparser_core(fp, fgetc(fp), buf, 0, db, database_new(), DB_COMMENT, lineno);
		default:
			fprintf(stderr, "%d: unexpected comment\n", lineno);
			return 0;
		}
	case '"': /* into string or out of string */
		switch (state) {
		case DB_COMMENT:
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_COMMENT, lineno);
		case DB_START:
			memset(buf, 0, DBLINEBUFMAX);
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_MODEL_REGEXP, lineno);
		case DB_MODEL_REGEXP:
			buf[buflen] = c;
			buflen++;
			tmpdb->model_regexp = malloc(buflen);
			strlcpy(tmpdb->model_regexp, buf, buflen);
			memset(buf, 0, DBLINEBUFMAX);
			return dbparser_core(fp, fgetc(fp), buf, buflen, db, tmpdb, DB_MODEL_REGEXP_SPC, lineno);
		case DB_UNIT_SPC:
			memset(buf, 0, DBLINEBUFMAX);
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_MODEL, lineno);
		case DB_MODEL:
			buf[buflen] = c;
			buflen++;
			tmpdb->model = malloc(buflen);
			strlcpy(tmpdb->model, buf, buflen);
			memset(buf, 0, DBLINEBUFMAX);
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_END, lineno);
		default:
			fprintf(stderr, "%d: unexpected `\"'\n", lineno);
			return 0;
		}
	case ' ':
	case '\t': /* space, split token */
		switch (state) {
		case DB_COMMENT:
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_COMMENT, lineno);
		case DB_ID:
			buflen++;
			buf[buflen + 1] = 0;
			tmpdb->id = strtonum(buf, 0, 256, &errstr);
			if (errstr)
				errx(1, "number of id is %s: %s", errstr, buf);
			memset(buf, 0, DBLINEBUFMAX);
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_ID_SPC, lineno);
		case DB_UNIT:
			buflen++;
			tmpdb->unit = malloc(buflen);
			strlcpy(tmpdb->unit, buf, buflen);
			memset(buf, 0, DBLINEBUFMAX);
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_UNIT_SPC, lineno);
		default:
			buf[buflen] = c;
			buflen++;
			return dbparser_core(fp, fgetc(fp), buf, buflen, db, tmpdb, state, lineno);
		}
	default:
		switch (state) {
		case DB_COMMENT:
			return dbparser_core(fp, fgetc(fp), buf, 0, db, tmpdb, DB_COMMENT, lineno);
		case DB_MODEL_REGEXP_SPC:
			memset(buf, 0, DBLINEBUFMAX);
			buf[0] = c;
			return dbparser_core(fp, fgetc(fp), buf, 1, db, tmpdb, DB_ID, lineno);
		case DB_ID_SPC:
			memset(buf, 0, DBLINEBUFMAX);
			buf[0] = c;
			return dbparser_core(fp, fgetc(fp), buf, 1, db, tmpdb, DB_UNIT, lineno);
		default:
			buf[buflen] = c;
			buflen++;
			return dbparser_core(fp, fgetc(fp), buf, buflen, db, tmpdb, state, lineno);
		}
	}
}	

/* simple wrapper */
int
dbparser(FILE *fp, hdd_database *db)
{
	char dbbuf[DBLINEBUFMAX];

	return dbparser_core(fp, fgetc(fp), dbbuf, 0, db, NULL, DB_START, 1);
}

static hdd_database*
search_hdd_model_from_db(char *model, hdd_database *db)
{
	hdd_database *p;
	regex_t regex;

	for (p = db->next; p->next; p = p->next) {
		if (regcomp(&regex, p->model_regexp, REG_EXTENDED | REG_NOSUB) != 0) {
			perror("regcomp");
			return NULL;
		}
		if (regexec(&regex, model, 0, NULL, 0) == 0)
			return p;
		regfree(&regex);
	}
	return NULL;
}

hdd_database*
search_hdd_model(char *dbfile, char *model)
{
	FILE *fp;
	
	hdd_database *db;

	db = database_new();
	if ((fp = fopen(dbfile, "r")) == NULL)
		return NULL;
	if (!dbparser(fp, db))
		return NULL;
	db = search_hdd_model_from_db(model, db);
	fclose(fp);
	if (db && db->id == 0)
		/* default value */
		db->id = SMART_TEMPERATURE;
	return db;
}
