PROG=   hddtemp
SRCS=   hddtemp.c database.c privsep.c

LDADD+=-lutil

NOMAN= yes

.include <bsd.prog.mk>
