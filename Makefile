PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

PROGS = calendar todo
MANS = ${PROGS:=.1}
OBJS = ${PROGS:=.o}

CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -Wall -Wextra ${CPPFLAGS}
LDFLAGS = -lm

all: ${PROGS}

calendar: calendar.o
	${CC} -o $@ calendar.o ${LDFLAGS}

todo: todo.o
	${CC} -o $@ todo.o ${LDFLAGS}

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
