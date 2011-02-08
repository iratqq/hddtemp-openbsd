/*
 * Copyright (c) 2004 Iwata <iratqq@gmail.com>
 * Copyright (c) 2003 Can Erkin Acar
 * Copyright (c) 2003 Anil Madhavapeddy <anil@recoil.org>
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

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <paths.h>
#include <signal.h>
#include <string.h>

#include "hddtemp.h"

int priv_fd = -1;
static volatile pid_t child_pid = -1;
volatile sig_atomic_t gotsig_chld = 0;

static void sig_pass_to_chld(int);
static void sig_chld(int);

static int  may_read(int, void *, size_t);
static void must_read(int, void *, size_t);
static void must_write(int, void *, size_t);

int
privsep_init(void)
{
	int socks[2], cmd;
	struct passwd *pw;

	/* Create sockets */
        if (socketpair(AF_LOCAL, SOCK_STREAM, PF_UNSPEC, socks) == -1)
                err(1, "socketpair() failed");

	if ((pw = getpwnam(PRIV_USER)) == NULL)
		errx(1, "no such user: " PRIV_USER);

	endpwent();

        child_pid = fork();
        if (child_pid < 0)
                errx(1, "fork() failed");

        if (!child_pid) {
		struct stat stb;
		gid_t gidset[1];

		if (stat(pw->pw_dir, &stb) == -1)
			err(1, "stat");
		if (stb.st_uid != 0 || (stb.st_mode & (S_IWGRP|S_IWOTH)) != 0)
			err(1, "bad privsep dir permissions");

		/* chroot, drop privs and return */
		if (chroot(pw->pw_dir) != 0) {
			fprintf(stderr, "no such directory: %s falling back to " _PATH_VAREMPTY "\n", pw->pw_dir);
			if (chroot(_PATH_VAREMPTY) != 0)
				err(1, "unable to chroot");
		}

		if (chdir("/") != 0)
			err(1, "unable to chdir");

		gidset[0] = pw->pw_gid;
		/* drop to _hddtemp */
		if (setgroups(1, gidset) == -1)
			err(1, "setgroups() failed");
		if (setegid(pw->pw_gid) == -1)
			err(1, "setegid() failed");
		if (setgid(pw->pw_gid) == -1)
			err(1, "setgid() failed");
		if (seteuid(pw->pw_uid) == -1)
			err(1, "seteuid() failed");
		if (setuid(pw->pw_uid) == -1)
			err(1, "setuid() failed");

                close(socks[0]);
                priv_fd = socks[1];

                return 0;
        }

	/* Father */
	/* Pass ALRM/TERM/HUP through to child, and accept CHLD */
        signal(SIGALRM, sig_pass_to_chld);
        signal(SIGTERM, sig_pass_to_chld);
        signal(SIGHUP,  sig_pass_to_chld);
        signal(SIGCHLD, sig_chld);

        setproctitle("[priv]");
        close(socks[1]);

	while (!gotsig_chld) {
		int len;
		char buf[BUFSIZ];
		int temp;

		if (may_read(socks[0], &cmd, sizeof(int)))
                        break;
		temp = smart_temperature();

		if (strcmp(hdd_db->unit, "C") == 0)
			temp = ftoc(temp);

		snprintf(buf, BUFSIZ - 1, "|%s|%s|%d|C|", hdd_dev, hdd_model, temp);
		len = strlen(buf);
		must_write(socks[0], &len, sizeof(int));
		must_write(socks[0], buf, len);
	}

	_exit(0);
}

/* If priv parent gets a TERM or HUP, pass it through to child instead */
static void
sig_pass_to_chld(int sig)
{
        int oerrno = errno;

        if (child_pid != -1)
                kill(child_pid, sig);
        errno = oerrno;
}

/* if parent gets a SIGCHLD, it will exit */
/* ARGSUSED */
static void
sig_chld(int sig)
{
        gotsig_chld = 1;
}

/* Read all data or return 1 for error.  */
static int
may_read(int fd, void *buf, size_t n)
{
        char *s = buf;
        ssize_t res, pos = 0;

        while (n > pos) {
                res = read(fd, s + pos, n - pos);
                switch (res) {
                case -1:
                        if (errno == EINTR || errno == EAGAIN)
                                continue;
                case 0:
                        return (1);
                default:
                        pos += res;
                }
        }
        return 0;
}

/* Read data with the assertion that it all must come through, or
 * else abort the process.  Based on atomicio() from openssh. */
static void
must_read(int fd, void *buf, size_t n)
{
        char *s = buf;
        ssize_t res, pos = 0;

        while (n > pos) {
                res = read(fd, s + pos, n - pos);
                switch (res) {
                case -1:
                        if (errno == EINTR || errno == EAGAIN)
                                continue;
                case 0:
                        _exit(0);
                default:
                        pos += res;
                }
        }
}

/* Write data with the assertion that it all has to be written, or
 * else abort the process.  Based on atomicio() from openssh. */
static void
must_write(int fd, void *buf, size_t n)
{
        char *s = buf;
        ssize_t res, pos = 0;

        while (n > pos) {
                res = write(fd, s + pos, n - pos);
                switch (res) {
                case -1:
                        if (errno == EINTR || errno == EAGAIN)
                                continue;
                case 0:
                        _exit(0);
                default:
                        pos += res;
                }
        }
}

int
priv_get_temperature(char *buf)
{
	int cmd = 1;
	int recv_len;
	char *recv_buf;

	/* wakeup */
	must_write(priv_fd, &cmd, sizeof(int));

	may_read(priv_fd, &recv_len, sizeof(int));

	recv_buf = malloc(recv_len);
	may_read(priv_fd, recv_buf, recv_len);

	memcpy(buf, recv_buf, recv_len);
	free(recv_buf);
	return recv_len;
}

