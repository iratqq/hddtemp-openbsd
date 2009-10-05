/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ken Hornstein.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the NetBSD
 *      Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define ATA_SMART_READ               0xd0 /* SMART read data */
#define ATA_SMART_THRESHOLD          0xd1 /* SMART read threshold */

/* device attribute */
struct attribute {
        u_int8_t  id;           /* Attribute ID */
        u_int16_t status;       /* Status flags */
        u_int8_t  value;        /* Attribute value */
        u_int8_t  raw[8];       /* Vendor specific */
} __packed;

/* read data sector */
struct smart_read {
        u_int16_t revision;     /* Data structure revision */
        struct attribute attribute[30]; /* Device attribute */
        u_int8_t  offstat;      /* Off-line data collection status */
#define SMART_OFFSTAT_NOTSTART  0x00
#define SMART_OFFSTAT_COMPLETE  0x02
#define SMART_OFFSTAT_SUSPEND   0x04
#define SMART_OFFSTAT_INTR      0x05
#define SMART_OFFSTAT_ERROR     0x06
        u_int8_t  selfstat;     /* Self-test execution status */
#define SMART_SELFSTAT_COMPLETE 0x00
#define SMART_SELFSTAT_ABORT    0x01
#define SMART_SELFSTAT_INTR     0x02
#define SMART_SELFSTAT_ERROR    0x03
#define SMART_SELFSTAT_UNKFAIL  0x04
#define SMART_SELFSTAT_ELFAIL   0x05
#define SMART_SELFSTAT_SRVFAIL  0x06
#define SMART_SELFSTAT_RDFAIL   0x07
#define SMART_SELFSTAT_PROGRESS 0x0f
        u_int16_t time;         /* Time to complete data collection activity */
        u_int8_t  vendor1;      /* Vendor specific */
        u_int8_t  offcap;       /* Off-line data collection capability */
#define SMART_OFFCAP_EXEC       0x01
#define SMART_OFFCAP_ABORT      0x04
#define SMART_OFFCAP_READSCAN   0x08
#define SMART_OFFCAP_SELFTEST   0x10
        u_int16_t smartcap;     /* SMART capability */
#define SMART_SMARTCAP_SAVE     0x01
#define SMART_SMARTCAP_AUTOSAVE 0x02
        u_int8_t  errcap;       /* Error logging capability */
#define SMART_ERRCAP_ERRLOG     0x01
        u_int8_t  vendor2;      /* Vendor specific */
        u_int8_t  shtime;       /* Short self-test polling time */
        u_int8_t  extime;       /* Extended self-test polling time */
        u_int8_t  res[12];      /* Reserved */
        u_int8_t  vendor3[125]; /* Vendor specific */
        u_int8_t  cksum;        /* Data structure checksum */
};

/* threshold entry */
struct threshold {
        u_int8_t  id;           /* Threshold ID */
        u_int8_t  value;        /* Threshold value */
        u_int8_t  reserve[10];
};

/* read thresholds sector */
struct smart_threshold {
        u_int16_t revision;     /* Data structure revision */
        struct threshold threshold[30];
        u_int8_t  reserve[18];
        u_int8_t  vendor[131];
        u_int8_t  cksum;        /* Data structure checksum */
};

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

/* default attribute id */
#define SMART_TEMPERATURE 194
int smart_temperature();

/* if you want to bind from any address, set NULL */
#define DEFAULT_HOST "localhost"
/* service name or port(string) */
#define DEFAULT_PORT "7634"
/* for security */
#define PRIV_USER "_hddtemp"

/* unit conversion of temperature */
#define ftoc(f) (int)(((double)f - 32.) / 1.8)
#define ctof(c) (int)(1.8 * (double)c + 32.)

#define DBLINEBUFMAX 256
#define HDDTEMP_DBFILE "/usr/local/share/hddtemp/hddtemp.db"

typedef struct hdd_database {
	struct hdd_database *next;
	char *model_regexp;
	int id;
	char *unit;
	char *model;
} hdd_database;

extern int hdd_fd;
extern char *hdd_model;
extern char *hdd_dev;
extern hdd_database *hdd_db;

hdd_database* search_hdd_model(char *, char *);

/*
 * main loop on daemon mode
 */
int privsep_init(void);
int priv_get_temperature(char *);
