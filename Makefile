PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

PROGS = calendar todo agenda
MANS = ${PROGS:=.1}
OBJS = ${PROGS:=.o} util.o

CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -g -O0 -Wall -Wextra ${CPPFLAGS}
LDFLAGS = -lm

all: ${PROGS}

calendar: calendar.o util.o
	${CC} -o $@ calendar.o util.o ${LDFLAGS}

todo: todo.o util.o
	${CC} -o $@ todo.o util.o ${LDFLAGS}

${OBJS}: util.h

.c.o:
	${CC} ${CFLAGS} -c $<

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	for prog in ${PROGS} ; do install -m 755 "$$prog" "${DESTDIR}${PREFIX}/bin/$$prog" ; done
	for man in ${MANS} ; do install -m 644 "$$man" "${DESTDIR}${MANPREFIX}/man1/$$man" ; done

uninstall:
	for prog in ${PROGS} ; do rm -f "${DESTDIR}${PREFIX}/bin/$$prog" ; done
	for man in ${MANS} ; do rm -f "${DESTDIR}${MANPREFIX}/man1/$$man" ; done

clean:
	-rm ${OBJS} ${PROGS}

.PHONY: all clean install uninstall
