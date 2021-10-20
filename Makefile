PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

MANS = calendar.1 todo.1 agenda.1
SRCS = calendar.c todo.c
OBJS = ${SRCS:.c=.o} util.o

CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -g -O0 -Wall -Wextra ${CPPFLAGS}
LDFLAGS = -lm

all: calendar todo

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
	install -m 755 calendar ${DESTDIR}${PREFIX}/bin/calendar
	install -m 755 agenda ${DESTDIR}${PREFIX}/bin/agenda
	install -m 755 todo ${DESTDIR}${PREFIX}/bin/todo
	install -m 644 calendar.1 ${DESTDIR}${MANPREFIX}/man1/calendar.1
	install -m 644 agenda.1 ${DESTDIR}${MANPREFIX}/man1/agenda.1
	install -m 644 todo.1 ${DESTDIR}${MANPREFIX}/man1/todo.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/calendar
	rm -f ${DESTDIR}${PREFIX}/bin/agenda
	rm -f ${DESTDIR}${PREFIX}/bin/todo
	rm -f ${DESTDIR}${MANPREFIX}/man1/calendar.1
	rm -f ${DESTDIR}${MANPREFIX}/man1/agenda.1
	rm -f ${DESTDIR}${MANPREFIX}/man1/todo.1

clean:
	-rm ${OBJS} calendar todo

.PHONY: all clean install uninstall
