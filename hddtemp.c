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

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <util.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <sys/wait.h>

#include <dev/ata/atareg.h>
#include <dev/ic/wdcreg.h>
#include <dev/ic/wdcevent.h>
#include <sys/ataio.h>

#include <getopt.h>

#include "hddtemp.h"

/* hdd device */
int hdd_fd;
/* hdd devname */
char *hdd_dev;
/* hdd model */
char *hdd_model;
/* database */
hdd_database *hdd_db;

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
void
ata_command(struct atareq *req)
{
        int error;

        if ((error = ioctl(hdd_fd, ATAIOCCOMMAND, req)) == -1)
                err(1, "ATAIOCCOMMAND failed");

        switch (req->retsts) {

        case ATACMD_OK:
                return;
        case ATACMD_TIMEOUT:
                errx(1, "ATA command timed out");
        case ATACMD_DF:
                errx(1, "ATA device returned a Device Fault");
        case ATACMD_ERROR:
                if (req->error & WDCE_ABRT)
                        errx(1, "ATA device returned Aborted Command");
                else
                        errx(1, "ATA device returned error register %0x",
                            req->error);
        default:
		errx(1, "ATAIOCCOMMAND returned unknown result code %d",
		     req->retsts);
        }
}


char *
ata_model()
{
	struct ataparams *inqbuf;
        struct atareq req;
        char inbuf[DEV_BSIZE], *s;
        u_int64_t capacity;

        memset(&inbuf, 0, sizeof(inbuf));
        memset(&req, 0, sizeof(req));

        inqbuf = (struct ataparams *) inbuf;

        req.flags = ATACMD_READ;
        req.command = WDCC_IDENTIFY;
        req.databuf = (caddr_t) inbuf;
        req.datalen = sizeof(inbuf);
        req.timeout = 1000;
	
	ata_command(&req);

        if (BYTE_ORDER == BIG_ENDIAN) {
                swap16_multi((u_int16_t *)inbuf, 10);
                swap16_multi(((u_int16_t *)inbuf) + 20, 3);
                swap16_multi(((u_int16_t *)inbuf) + 47, sizeof(inbuf) / 2 - 47);
        }

	if (!((inqbuf->atap_config & WDC_CFG_ATAPI_MASK) == WDC_CFG_ATAPI &&
              ((inqbuf->atap_model[0] == 'N' &&
		inqbuf->atap_model[1] == 'E') ||
               (inqbuf->atap_model[0] == 'F' &&
		inqbuf->atap_model[1] == 'X')))) {
                swap16_multi((u_int16_t *)(inqbuf->atap_model),
			     sizeof(inqbuf->atap_model) / 2);
                swap16_multi((u_int16_t *)(inqbuf->atap_serial),
			     sizeof(inqbuf->atap_serial) / 2);
                swap16_multi((u_int16_t *)(inqbuf->atap_revision),
			     sizeof(inqbuf->atap_revision) / 2);
        }

	/*
         * Strip blanks off of the info strings.
         */
	for (s = &inqbuf->atap_model[sizeof(inqbuf->atap_model) - 1];
	     s >= (char *)inqbuf->atap_model && *s == ' '; s--)
                *s = '\0';
	s = strdup(inqbuf->atap_model);
	return s;
}

int
smart_temperature()
{
	struct atareq req;
	struct smart_read attr_val;
	struct smart_threshold attr_thr;
        struct attribute *attr;
        struct threshold *thr;
	int i;

	if (hdd_db == NULL)
		return 0;

	memset(&req, 0, sizeof(req));
        memset(&attr_val, 0, sizeof(attr_val)); /* XXX */
        memset(&attr_thr, 0, sizeof(attr_thr)); /* XXX */

	req.command = ATAPI_SMART;
        req.cylinder = 0xc24f;          /* LBA High = C2h, LBA Mid = 4Fh */
        req.timeout = 1000;

        req.features = ATA_SMART_READ;
        req.flags = ATACMD_READ;
        req.databuf = (caddr_t)&attr_val;
        req.datalen = sizeof(attr_val);
        ata_command(&req);

        req.features = ATA_SMART_THRESHOLD;
        req.flags = ATACMD_READ;
        req.databuf = (caddr_t)&attr_thr;
        req.datalen = sizeof(attr_thr);
        ata_command(&req);

        attr = attr_val.attribute;
        thr = attr_thr.threshold;

        for (i = 0; i < 30; i++) {
		if (thr[i].id == hdd_db->id) {
			return attr[i].value;
		}
        }
	return INT_MAX;
}

extern const char *__progname;		/* from crt0.o */

usage()
{
	fprintf(stderr, "%s [-d] [-f database] device\n", __progname);
	exit(1);
}

/*
 * The sockets that the server is listening; this is used in the SIGHUP
 * signal handler.
 */
#define MAX_LISTEN_SOCKS 16
int listen_socks[MAX_LISTEN_SOCKS];
int num_listen_socks = 0;

/*
 * Close all listening sockets
 */
static void
close_listen_socks(void)
{
        int i;

        for (i = 0; i < num_listen_socks; i++)
                close(listen_socks[i]);
        num_listen_socks = -1;
}

/*
 * SIGCHLD handler.  This is called whenever a child dies.  This will then
 * reap any zombies left by exited children.
 */
static void
main_sigchld_handler(int sig)
{
        int save_errno = errno;
        pid_t pid;
        int status;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0 ||
            (pid < 0 && errno == EINTR))
                ;

        signal(SIGCHLD, main_sigchld_handler);
        errno = save_errno;
}

/*
 * Copyright (c) 2000, 2001, 2002 Markus Friedl.  All rights reserved.
 * Copyright (c) 2002 Niels Provos.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
int client_linsten()
{
	struct sockaddr *sa;
        char *name = DEFAULT_HOST;
	char *service = DEFAULT_PORT;
        struct addrinfo hints;
        struct addrinfo *res, *res0;
	int listen_sock, maxfd;
	socklen_t fromlen;
	int sock_in = -1, sock_out = -1, newsock = -1;
	int error;
	u_int8_t salen;
	char ntop[NI_MAXHOST], strport[NI_MAXSERV];
	int fdsetsz;
	int on = 1;
	fd_set *fdset;
	struct sockaddr_storage from;
	int ret;
	int i;
	int pid;
	int readlen;
	char buf[BUFSIZ];
	int fd;
	int outlen;

	/*
         * getaddrinfo() case.  You can get IPv6 address and IPv4 address
         * at the same time.
         */
        memset(&hints, 0, sizeof(hints));
        /* set-up hints structure */
        hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_PASSIVE;
        hints.ai_socktype = SOCK_STREAM;

        error = getaddrinfo(name, service, &hints, &res);
        if (error) {
                perror(gai_strerror(error));
		exit(1);
	}

        /* Privilege separation begins here */
        if (privsep_init()) {
                fprintf(stderr, "unable to privsep");
                exit(1);
        }

	res0 = res;

	for ( ; res; res = res->ai_next) {
		sa = res->ai_addr;
		salen = res->ai_addrlen;

		if (res->ai_family != AF_INET && res->ai_family != AF_INET6)
			continue;

		if (num_listen_socks >= MAX_LISTEN_SOCKS) {
			fprintf(stderr,
				"Too many listen sockets. "
				"Enlarge MAX_LISTEN_SOCKS\n");
			exit(1);
		}
		if (getnameinfo(sa, salen,
				ntop, sizeof(ntop), strport, sizeof(strport),
				NI_NUMERICHOST|NI_NUMERICSERV) != 0) {
			fprintf(stderr, "getnameinfo failed\n");
			continue;
		}
		/* Create socket for listening. */
		listen_sock = socket(res->ai_family, res->ai_socktype,
				     res->ai_protocol);
		if (listen_sock < 0) {
			/* kernel may not support ipv6 */
			fprintf(stderr, "socket: %.100s\n", strerror(errno));
			continue;
		}

		/*
		 * Set socket options.
		 * Allow local port reuse in TIME_WAIT.
		 */
		if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
			       &on, sizeof(on)) == -1)
			fprintf(stderr, "setsockopt SO_REUSEADDR: %s\n", strerror(errno));

		if (bind(listen_sock, sa, salen) < 0) {
			fprintf(stderr, "Bind to port %s on %s failed: %.200s.\n",
				strport, ntop, strerror(errno));
			close(listen_sock);
			continue;
		}
		listen_socks[num_listen_socks] = listen_sock;
		num_listen_socks++;

		/* Start listening on the port. */
#if 0
		printf("Server listening on %s port %s.\n", ntop, strport);
#endif
		if (listen(listen_sock, 127) < 0) {
			fprintf(stderr, "listen: %.100s\n", strerror(errno));
			exit(1);
		}
	}

	freeaddrinfo(res0);

	/* Arrange SIGCHLD to be caught. */
	signal(SIGCHLD, main_sigchld_handler);

	/* setup fd set for listen */
	fdset = NULL;
	maxfd = 0;
	for (i = 0; i < num_listen_socks; i++)
		if (listen_socks[i] > maxfd)
			maxfd = listen_socks[i];

	/*
	 * Stay listening for connections until the system crashes or
	 * the daemon is killed with a signal.
	 */
	for ( ; ; ) {
		if (fdset != NULL)
			free(fdset);
		fdsetsz = howmany(maxfd + 1, NFDBITS) * sizeof(fd_mask);
		fdset = (fd_set *)malloc(fdsetsz);
		memset(fdset, 0, fdsetsz);

		for (i = 0; i < num_listen_socks; i++)
			FD_SET(listen_socks[i], fdset);

		/* Wait in select until there is a connection. */
		ret = select(maxfd + 1, fdset, NULL, NULL, NULL);
		if (ret < 0 && errno != EINTR)
			fprintf(stderr, "select: %.100s\n", strerror(errno));
		if (ret < 0)
			continue;
		for (i = 0; i < num_listen_socks; i++) {
			if (!FD_ISSET(listen_socks[i], fdset))
				continue;
			fromlen = sizeof(from);
			newsock = accept(listen_socks[i], (struct sockaddr *)&from,
					 &fromlen);
			if (newsock < 0) {
				if (errno != EINTR && errno != EWOULDBLOCK)
					fprintf(stderr, "accept: %.100s\n", strerror(errno));
				continue;
			}
			/*
			 * Normal production daemon.  Fork, and have
			 * the child process the connection. The
			 * parent continues listening.
			 */
			if ((pid = fork()) == 0) {
				/*
				 * Child.  Close the listening and max_startup
				 * sockets.  Start using the accepted socket.
				 * Reinitialize logging (since our pid has
				 * changed).  We break out of the loop to handle
				 * the connection.
				 */
				close_listen_socks();
				sock_in = newsock;
				sock_out = newsock;
				break;
			}
			/* Parent.  Stay in the loop. */
			if (pid < 0)
				fprintf(stderr, "fork: %.100s\n", strerror(errno));
			
			/* Close the new socket (the child is now taking care of it). */
			close(newsock);
		}
		/* child process check (or debug mode) */
		if (num_listen_socks < 0)
			break;
	}

	/* This is the child processing a new connection. */
        setproctitle("%s", "[accepted]");

	outlen = 0;

	/* pass to priv server */
	memset(buf, 0, BUFSIZ);
	readlen = priv_get_temperature(buf);
	/* revieve to client */
	write(sock_out, buf, readlen);

	if (readlen < 0)
		fprintf(stderr, "read: %.100s\n", strerror(errno));

	close(sock_in);
	close(sock_out);
	_exit(0);
}

int
main(int argc, char *argv[])
{
	int temp;
	int ch;
        char dvname_store[MAXPATHLEN];
	int daemon_mode = 0;
	char *dbfile = NULL;

	while ((ch = getopt(argc, argv, "df:")) != -1) {
		switch (ch) {
		case 'd':
			daemon_mode = 1;
			break;
		case 'f':
			dbfile = strdup(optarg);
			break;
		default:
			break;
		}
	}

        argv += optind;
        argc -= optind;

        if (argc == 0)
                usage();

	if (!dbfile)
		dbfile = HDDTEMP_DBFILE;

	hdd_dev = strdup(argv[0]);

        /*
         * Open the device
         */
        hdd_fd = opendisk(hdd_dev, O_RDWR, dvname_store, sizeof(dvname_store), 0);
        if (hdd_fd == -1) {
                if (errno == ENOENT) {
                        /*
                         * Device doesn't exist.  Probably trying to open
                         * a device which doesn't use disk semantics for
                         * device name.  Try again, specifying "cooked",
                         * which leaves off the "r" in front of the device's
                         * name.
                         */
                        hdd_fd = opendisk(hdd_dev, O_RDWR, dvname_store,
                            sizeof(dvname_store), 1);
                        if (hdd_fd == -1)
                                err(1, "%s", hdd_dev);
                } else
                        err(1, "%s", hdd_dev);
        }

	hdd_model = ata_model();

	/* database open */
	hdd_db = search_hdd_model(dbfile, hdd_model);
	if (hdd_db == NULL) {
		fprintf(stderr, "cannot find from database: \"%s\"\n", hdd_model);
		exit(1);
	}

	/* stand alone */
	if (!daemon_mode) {
		temp = smart_temperature();

		if (strcmp(hdd_db->unit, "C") == 0)
			temp = ftoc(temp);

		printf("%s: %s: %dC\n", hdd_dev, hdd_model, temp);
		return 0;
	}

	/* daemon_mode */
	if (daemon(0, 1)) {
		errx(2, "fork failed");
	}

	client_linsten();
}
