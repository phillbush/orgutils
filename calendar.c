#include <sys/time.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "util.h"

/* day pattern */
struct DPattern {
	/*
	 * This structure express a day pattern.  For convenience, let's
	 * express a DPattern entry as YYYY/MM/DD/m/w, where:
	 * - year is YYYY (1 to INT_MAX)
	 * - month is MM (1 to 12)
	 * - monthday is DD (1 to 31)
	 * - monthweek is m (-5 to 5)
	 * - weekday is w (1-Monday to 7-Sunday)
	 *
	 * For example, 2020/03/11/2/3 matches 11 March 2020, which was
	 * a Wednesday (3) on the second week of March.  This date can
	 * also be matched by 2020/03/11/-4/3, because this date was on
	 * the fourth to last week of that month.  A zero value matches
	 * anything.  For example:
	 * - 0000/12/25/0/0 matches 25 December of every year.
	 * - 0000/05/00/2/7 matches the second Sunday of May.
	 * - 2020/03/11/2/3 matches 11 March 2020.
	 */

	struct DPattern *next;          /* pointer to next day on linked list */
	int year;
	int month;
	int monthday;
	int monthweek;
	int weekday;
};

/* event */
struct Event {
	struct Event *next;             /* pointer to next event on linked list */
	struct DPattern *days;             /* list of day patterns */
	char *name;                     /* event name */
	char *filename;                 /* file event came from */
};

/* collection of events */
struct Calendar {
	struct Event *head, *tail;      /* pointers to singly linked list of events */
};

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: calendar [-l] [-T YYYY-MM-DD] [-n num] [file ...]\n");
	exit(1);
}

/* check if c is separator */
static int
isseparator(int c)
{
	return c == '-' || c == '.' || c == '/';
}

/* get patterns for event s; also return its name */
static int
parseline(void *p, char *line, char *filename)
{
	struct Calendar *calendar = p;
	struct Event *ev;
	struct DPattern *patt, *oldpatt;
	struct DPattern d;
	struct tm tm;
	int n;
	char *t, *end;

	patt = NULL;
	for (;;) {
		d = (struct DPattern){
			.next = NULL,
			.year = 0,
			.month = 0,
			.monthday = 0,
			.monthweek = 0,
			.weekday = 0,
		};
		while (isspace(*(unsigned char *)line))
			line++;
		n = strtol(line, &end, 10);
		if (n > 0 && isseparator(*end)) {
			/* got numeric year or month */
			d.month = n;
			line = end + 1;
			n = strtol(line, &end, 10);
			if (n > 0 && isseparator(*end)) {
				/* got numeric month after year */
				d.year = d.month;
				d.month = n;
				line = end + 1;
			} else if ((t = strptime(line, "%b", &tm)) != NULL && isseparator(*t)){
				/* got month name after year */
				d.year = d.month;
				d.month = tm.tm_mon + 1;
				line = t + 1;
			}
		} else if ((t = strptime(line, "%b", &tm)) != NULL && isseparator(*t)) {
			/* got month name */
			d.month = tm.tm_mon + 1;
			line = t + 1;
		}
		n = strtol(line, &end, 10);
		if (n > 0 && *end != '\0') {
			/* got month day */
			d.monthday = n;
			line = end;
		}
		if ((t = strptime(line, "%a", &tm)) != NULL) {
			/* got week day */
			d.weekday = tm.tm_wday + 1;
			line = t;
		}
		if (d.monthday == 0 && d.weekday == 0)
			break;
		n = strtol(line, &end, 10);
		if (n >= -5 && n <= 5 && *end != '\0') {
			d.monthweek = n;
			line = end;
		}
		oldpatt = patt;
		patt = emalloc(sizeof(*patt));
		*patt = d;
		patt->next = oldpatt;
		while (isspace(*(unsigned char *)line))
			line++;
		if (*line == ',') {
			line++;
		} else {
			break;
		}
	}
	if (patt == NULL)
		return -1;
	while (isspace(*(unsigned char *)line))
		line++;
	ev = emalloc(sizeof(*ev));
	ev->next = NULL;
	ev->days = patt;
	ev->name = estrdup(line);
	ev->filename = filename;
	if (calendar->head == NULL)
		calendar->head = ev;
	if (calendar->tail != NULL)
		calendar->tail->next = ev;
	calendar->tail = ev;
	return 0;
}

/* check if event occurs today */
static int
occurstoday(struct Date *today, struct DPattern *patts)
{
	struct DPattern *d;

	for (d = patts; d != NULL; d = d->next) {
		if ((d->year == 0 || d->year == today->y) &&
		    (d->month == 0 || d->month == today->m) &&
		    (d->monthday == 0 || d->monthday == today->d) &&
		    (d->weekday == 0 || d->weekday == today->w + 1) &&
		    (d->monthweek == 0 ||
		     (d->monthweek < 0 && d->monthweek == today->nmw) || 
		     (d->monthweek == today->pmw))) {
			return 1;
		}
	}
	return 0;
}

/* print events for today and after days */
static void
printcalendar(struct Calendar *calendar, struct Date *today, int after, int lflag, int prefix)
{
	struct tm tm;
	struct Event *ev;
	char buf1[128];
	char buf2[128];

	buf1[0] = buf2[0] = '\0';
	while (after-- >= 0) {
		tm.tm_year = today->y - 1900;
		tm.tm_wday = today->w;
		tm.tm_mday = today->d;
		tm.tm_mon = today->m - 1;
		if (lflag) {
			strftime(buf1, sizeof(buf1), "%A", &tm);
			strftime(buf2, sizeof(buf2), "%d %B %Y", &tm);
			printf("%-10s %s\n", buf1, buf2);
		} else {
			strftime(buf1, sizeof(buf1), "%m-%d", &tm);
		}
		for (ev = calendar->head; ev != NULL; ev = ev->next) {
			if (occurstoday(today, ev->days)) {
				if (!lflag)
					printf("%s", buf1);
				printf("\t");
				if (prefix)
					printf("%s: ", ev->filename);
				printf("%s\n", ev->name);
			}
		}
		incrdate(today);
	}
}

/* free events, their name and day patterns */
static void
freecalendar(struct Calendar *calendar)
{
	struct Event *e;
	struct DPattern *d;

	while (calendar->head) {
		e = calendar->head;
		calendar->head = calendar->head->next;
		while (e->days) {
			d = e->days;
			e->days = e->days->next;
			free(d);
		}
		free(e->name);
		free(e);
	}
}

/* calendar: print upcoming events */
int
main(int argc, char *argv[])
{
	static struct Calendar calendar = {
		.head = NULL,
		.tail = NULL,
	};
	struct Date today;
	int after = 1;          /* number of days after today */
	int lflag = 0;          /* whether to print in long format */
	int exitval = 0;
	int ch;

	if (gettoday(&today) == -1)
		err(1, NULL);
	if (today.w == FRIDAY)
		after = 3;
	else if (today.w == SATURDAY)
		after = 2;
	while ((ch = getopt(argc, argv, "ln:T:")) != -1) {
		switch (ch) {
		case 'l':
			lflag = 1;
			break;
		case 'n':
			after = strtonum(optarg, 0, INT_MAX);
			break;
		case 'T':
			if (strtodate(&today, optarg, NULL) == -1)
				errx(1, "improper argument date: %s", optarg);
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (readinput(parseline, &calendar, argc, argv) == -1)
		exitval = 1;
	printcalendar(&calendar, &today, after, lflag, argc > 1);
	freecalendar(&calendar);
	return exitval;
}
