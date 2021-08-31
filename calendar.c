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

#define SECS_PER_DAY  ((time_t)(24 * 60 * 60))
#define DAYS_PER_WEEK 7
#define MAX_DAYS      35
#define MIDDAY        12
#define YEAR_ZERO     1900
#define isleap(y)     ((!((y) % 4) && ((y) % 100)) || !((y) % 400))

/* day or day pattern */
struct Day {
	/*
	 * This structure express both a day and a day pattern.  For
	 * convenience, let's express a Day entry as YYYY-MM-DD-m-w,
	 * where:
	 * - year is YYYY (1 to INT_MAX)
	 * - month is MM (1 to 12)
	 * - monthday is DD (1 to 31)
	 * - monthweek is m (-5 to 5)
	 * - weekday is w (1-Monday to 7-Sunday)
	 *
	 * A day is expressed with all values nonzero.  For example,
	 * 2020/03/11/2/3 represents 11 March 2020, which was a
	 * Wednesday (3) on the second week of March.  This date can
	 * also be represented as 2020/03/11/-4/3, because this date
	 * was on the fourth to last week of that month.
	 *
	 * A struct Day pattern can have any value as zero.  A zero
	 * value matches anything.  For example:
	 * - 0000/12/25/0/0 matches 25 December of every year.
	 * - 0000/05/00/2/7 matches the second Sunday of May.
	 * - 2020/03/11/2/3 matches 11 March 2020.
	 */
	struct Day *next;       /* pointer to next day on linked list */
	int year;
	int month;
	int monthday;
	int monthweek;
	int weekday;
};

/* event */
struct Event {
	struct Event *next;     /* pointer to next event on linked list */
	struct Day *days;       /* list of day patterns */
	char *name;             /* event name */
};

/* table of day in month, indexed by whether year is leap and month number */
static const int days_in_month[2][13] = {
	{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	{0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
};

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: calendar [-ly] [-A num] [-B num] [-t yyyymmdd] [-N num] [file ...]\n");
	exit(1);
}

/* call calloc checking for error */
static void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return p;
}

/* call strdup checking for error */
static char *
estrdup(const char *s)
{
	char *t;

	if ((t = strdup(s)) == NULL)
		err(1, "strdup");
	return t;
}

/* convert string value to int between min and max; exit on error */
static int
strtonum(const char *s, int min, int max)
{
	long n;
	char *ep;

	errno = 0;
	n = strtol(s, &ep, 10);
	if (s[0] == '\0' || *ep != '\0')
		goto error;
	if (errno == ERANGE || n < min || n > max)
		goto error;
	return (int)n;
error:
	errno = EINVAL;
	err(1, "%s", s);
	return -1;
}

/* convert YYYYMMDD to time */
static time_t
strtotime(const char *s)
{
	struct tm *tmorig;
	struct tm tm;
	size_t len;
	time_t t;
	char *ep;

	if ((t = time(NULL)) == -1)
		err(1, NULL);
	if ((tmorig = localtime(&t)) == NULL)
		err(1, NULL);
	tm = *tmorig;
	len = strlen(s);
	if (len == 2 || len == 1)
		ep = strptime(s, "%d", &tm);
	else if (len == 4)
		ep = strptime(s, "%m%d", &tm);
	else
		ep = strptime(s, "%Y%m%d", &tm);
	if (s[0] == '\0' || ep == NULL || ep[0] != '\0')
		goto error;
	tm.tm_hour = MIDDAY;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	tm.tm_isdst = -1;
	t = mktime(&tm);
	if (t == -1)
		goto error;
	return t;
error:
	errno = EINVAL;
	err(1, "%s", s);
	return (time_t)-1;
}

/* set today time for 12:00; also set number of days after today */
static void
settoday(time_t *today, int *after)
{

	struct tm *tmorig;
	struct tm tm;
	time_t t;

	if ((t = time(NULL)) == -1)
		err(1, NULL);
	if ((tmorig = localtime(&t)) == NULL)
		err(1, NULL);
	tm = *tmorig;
	tm.tm_hour = MIDDAY;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	tm.tm_isdst = -1;
	*today = mktime(&tm);
	switch (tm.tm_wday) {
	case 5:
		*after = 3;
		break;
	case 6:
		*after = 2;
		break;
	default:
		*after = 1;
		break;
	}
}

/* check if c is separator */
static int
isseparator(int c)
{
	return c == '-' || c == '.' || c == '/' || c == ' ';
}

/* get patterns for event s; also return its name */
static struct Day *
getpatterns(char *s, char **name)
{
	struct tm tm;
	struct Day *patt, *oldpatt;
	struct Day d;
	int n;
	char *t, *end;

	patt = NULL;
	for (;;) {
		memset(&d, 0, sizeof(d));
		while (isspace(*(unsigned char *)s)) {
			s++;
		}
		n = strtol(s, &end, 10);
		if (n > 0 && isseparator(*end)) {
			/* got numeric year or month */
			d.month = n;
			s = end + 1;
			n = strtol(s, &end, 10);
			if (n > 0 && isseparator(*end)) {
				/* got numeric month after year */
				d.year = d.month;
				d.month = n;
				s = end + 1;
			} else if ((t = strptime(s, "%b", &tm)) != NULL && isseparator(*t)){
				/* got month name after year */
				d.year = d.month;
				d.month = tm.tm_mon + 1;
				s = t + 1;
			}
		} else if ((t = strptime(s, "%b", &tm)) != NULL && isseparator(*t)) {
			/* got month name */
			d.month = tm.tm_mon + 1;
			s = t + 1;
		}
		n = strtol(s, &end, 10);
		if (n > 0 && *end != '\0') {
			/* got month day */
			d.monthday = n;
			s = end;
		}
		if ((t = strptime(s, "%a", &tm)) != NULL) {
			/* got week day */
			d.weekday = tm.tm_wday + 1;
			s = t;
		}
		if (d.monthday == 0 && d.weekday == 0)
			break;
		n = strtol(s, &end, 10);
		if (n >= -5 && n <= 5 && *end != '\0') {
			d.monthweek = n;
			s = end;
		}
		oldpatt = patt;
		patt = ecalloc(1, sizeof(*patt));
		*patt = d;
		patt->next = oldpatt;
		while (isspace(*(unsigned char *)s)) {
			s++;
		}
		if (*s == ',') {
			s++;
			while (isspace(*(unsigned char *)s)) {
				s++;
			}
		} else {
			break;
		}
	}
	*name = s;
	(*name)[strcspn(*name, "\n")] = 0;
	return patt;
}

/* get events for file fp */
static void
getevents(FILE *fp, struct Event **head, struct Event **tail)
{
	struct Event *ev;
	struct Day *patt;
	char buf[BUFSIZ];
	char *name;

	while (fgets(buf, BUFSIZ, fp) != NULL) {
		if ((patt = getpatterns(buf, &name)) != NULL) {
			ev = ecalloc(1, sizeof(*ev));
			ev->next = NULL;
			ev->days = patt;
			ev->name = estrdup(name);
			if (*head == NULL)
				*head = ev;
			if (*tail != NULL)
				(*tail)->next = ev;
			*tail = ev;
		}
	}
}

/* get week of year */
static int
getwofy(int yday, int wday)
{
	return (yday + DAYS_PER_WEEK - (wday ? (wday - 1) : (DAYS_PER_WEEK - 1))) / DAYS_PER_WEEK;
}

/* check if event occurs today */
static int
occurstoday(struct Event *ev, struct tm *tm, int thiswofm, int lastwofm)
{
	struct Day *d;

	for (d = ev->days; d != NULL; d = d->next) {
		if ((d->year == 0 || d->year == tm->tm_year + YEAR_ZERO) &&
		    (d->month == 0 || d->month == tm->tm_mon + 1) &&
	       	    (d->monthday == 0 || d->monthday == tm->tm_mday) &&
	       	    (d->weekday == 0 || d->weekday == tm->tm_wday + 1) &&
	       	    (d->monthweek == 0 ||
	       	     (d->monthweek < 0 && d->monthweek == -1 * (lastwofm - thiswofm - 1)) || 
	       	     (d->monthweek == thiswofm))) {
	       	    	return 1;
	       	}
	}
	return 0;
}

/* print events for today and after days */
static void
printevents(struct Event *evs, time_t today, int after, int lflag, int yflag)
{
	struct tm *tmorig;
	struct tm tm;
	struct Event *ev;
	int wofy;       /* week of year of first day of month */
	int thiswofm;   /* this week of current month */
	int lastwofm;   /* last week of current month */
	int n;
	char buf1[BUFSIZ];
	char buf2[BUFSIZ];

	buf1[0] = buf2[0] = '\0';
	while (after-- >= 0) {
		tmorig = localtime(&today);
		tm = *tmorig;
		n = days_in_month[isleap(tm.tm_year + YEAR_ZERO)][tm.tm_mon + 1];
		wofy = getwofy(tm.tm_yday - tm.tm_mday + 1, (tm.tm_wday - tm.tm_mday + 1 + MAX_DAYS) % DAYS_PER_WEEK);
		thiswofm = getwofy(tm.tm_yday, tm.tm_wday) - wofy + 1;
		lastwofm = getwofy(tm.tm_yday + n - tm.tm_mday + 1, (tm.tm_wday + n - tm.tm_mday + 1 + MAX_DAYS) % DAYS_PER_WEEK) - wofy + 1;
		if (lflag) {
			strftime(buf1, sizeof(buf1), "%A", &tm);
			strftime(buf2, sizeof(buf2), "%d %B %Y", &tm);
			printf("%-10s %s\n", buf1, buf2);
		} else if (yflag) {
			strftime(buf1, sizeof(buf1), "%Y-%m-%d", &tm);
		} else {
			strftime(buf1, sizeof(buf1), "%m-%d", &tm);
		}
		for (ev = evs; ev != NULL; ev = ev->next) {
			if (occurstoday(ev, &tm, thiswofm, lastwofm)) {
				if (!lflag)
					printf("%s", buf1);
				printf("\t%s\n", ev->name);
			}
		}
		today += SECS_PER_DAY;
	}
}

/* free events, their name and day patterns */
static void
freeevents(struct Event *evs)
{
	struct Event *e;
	struct Day *d;

	while (evs) {
		e = evs;
		evs = evs->next;
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
	static struct Event *head, *tail;
	FILE *fp;
	time_t today;           /* seconds of 12:00:00 of today since epoch*/
	int ndays;              /* number of days to adjust today */
	int before;             /* number of days before today */
	int after;              /* number of days after today */
	int lflag;              /* whether to print in long format */
	int yflag;              /* whether to print year in regular format */
	int exitval;
	int ch;

	ndays = before = lflag = yflag = 0;
	settoday(&today, &after);
	while ((ch = getopt(argc, argv, "A:B:lN:t:y")) != -1) {
		switch (ch) {
		case 'A':
			after = strtonum(optarg, 0, INT_MAX);
			break;
		case 'B':
			before = strtonum(optarg, 0, INT_MAX);
			break;
		case 'l':
			lflag = 1;
			break;
		case 'N':
			ndays = strtonum(optarg, INT_MIN, INT_MAX);
			break;
		case 't':
			today = strtotime(optarg);
			break;
		case 'y':
			yflag = 1;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	today += ndays * SECS_PER_DAY;
	after += before;
	today -= before * SECS_PER_DAY;
	head = tail = NULL;
	exitval = 0;
	if (argc == 0) {
		getevents(stdin, &head, &tail);
	} else {
		for (; *argv != NULL; argv++) {
			if (strcmp(*argv, "-") == 0) {
				getevents(stdin, &head, &tail);
				continue;
			}
			if ((fp = fopen(*argv, "r")) == NULL) {
				warn("%s", *argv);
				exitval = 1;
				continue;
			}
			getevents(fp, &head, &tail);
			fclose(fp);
		}
	}
	printevents(head, today, after, lflag, yflag);
	freeevents(head);
	return exitval;
}
