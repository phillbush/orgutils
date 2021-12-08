#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

#define DAYSPERWEEK 7
#define MAX_DAYS      35
#define ISLEAP(y)     ((!((y) % 4) && ((y) % 100)) || !((y) % 400))

/* table of day in month, indexed by whether year is leap and month number */
static const int daytab[2][13] = {
	{0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	{0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
};

/* read lines from fp */
static int
getlines(Parser fun, void *p, FILE *fp, char *filename)
{
	ssize_t linelen = 0;
	size_t linesize = 0;
	size_t linenum = 0;
	int retval = 0;
	char *line = NULL;
	char *s;

	while ((linelen = getline(&line, &linesize, fp)) != -1) {
		linenum++;
		if (linelen > 0 && line[linelen - 1] == '\n')
			line[linelen - 1] = '\0';
		for (s = line; isspace(*s); s++)
			;
		if (*s == '#' || *s == '\0')
			continue;
		if ((*fun)(p, s, filename) == -1) {
			warnx("%s:%zu: invalid line", filename, linenum);
			retval = -1;
		}
	}
	free(line);
	if (ferror(fp)) {
		warn(NULL);
		clearerr(fp);
		return -1;
	}
	return retval;
}

/* get week of year */
static int
getweeknum(int day, int wday)
{
	return 1 + (day + DAYSPERWEEK - wday) / DAYSPERWEEK;
}

/* struct tm to struct Date */
static void
tmtodate(struct tm *tm, struct Date *d)
{
	d->y = tm->tm_year + 1900;
	d->m = tm->tm_mon + 1;
	d->d = tm->tm_mday;
	d->w = tm->tm_wday;
	d->pmw = getweeknum(d->d, d->w);
	d->nmw = -1 -(getweeknum(daytab[ISLEAP(d->y)][d->m] + 1, d->w - d->d + daytab[ISLEAP(d->y)][d->m] + 1 + MAX_DAYS) - d->pmw);
}

/* convert struct tm to unix julian day (days since unix epoch) */
int
datetojulian(struct Date *d)
{
	int y, m;

	if (d->y < 1 || d->m < 1 || d->m > 12 || d->d < 1 || d->d > daytab[ISLEAP(d->y)][d->m])
		return -1;
	y = d->y;
	m = d->m;
	if (m < 3) {
		y--;
		m += 12;
	}
	return (y * 365) + (y / 4) - (y / 100) + (y / 400) - 719468 + (m * 153 + 3) / 5 - 92 + d->d - 1;
}

/* call malloc checking for error */
void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

/* call calloc checking for error */
void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return p;
}

/* call strdup checking for error */
char *
estrdup(const char *s)
{
	char *t;

	if ((t = strdup(s)) == NULL)
		err(1, "strdup");
	return t;
}

/* date string in YYYY-MM-DD format to date structure */
int
strtodate(struct Date *d, const char *s, const char **endptr)
{
	struct tm *tmorig;
	struct tm tm;
	time_t t;
	int format;
	const char *ep;

	format = 1;
	ep = s;
	while (isdigit(*ep))
		ep++;
	if (*ep == '-') {
		format++;
		while (isdigit(*ep))
			ep++;
		if (*ep == '-') {
			format++;
		}
	}
	switch (format) {
	if ((t = time(NULL)) == -1)
		err(1, NULL);
	if ((tmorig = localtime(&t)) == NULL)
		err(1, NULL);
	tm = *tmorig;
	case 3:
		ep = strptime(s, "%Y-%m-%d", &tm);
		break;
	case 2:
		ep = strptime(s, "%m-%d", &tm);
		break;
	default:
		ep = strptime(s, "%d", &tm);
		break;
	}
	if (s[0] == '\0' || ep == NULL)
		goto error;
	if (endptr)
		*endptr = ep;
	tmtodate(&tm, d);
	return 0;
error:
	return -1;
}

/* get time for today, at 12:00 */
int
gettoday(struct Date *d)
{
	struct tm *tm;
	time_t t;

	if ((t = time(NULL)) == -1)
		return -1;
	if ((tm = localtime(&t)) == NULL)
		return -1;
	tmtodate(tm, d);
	return 0;
}

/* read input from files or stdin; return -1 on error */
int
readinput(Parser fun, void *p, int argc, char *argv[])
{
	FILE *fp;
	int retval = 0;

	if (argc == 0) {
		if (getlines(fun, p, stdin, "stdin") == -1) {
			retval = -1;
		}
	}
	for (; *argv != NULL; argv++) {
		if (strcmp(*argv, "-") == 0) {
			if (getlines(fun, p, stdin, "stdin") == -1) {
				retval = -1;
			}
			continue;
		}
		if ((fp = fopen(*argv, "r")) == NULL) {
			warn("%s", *argv);
			retval = 1;
			continue;
		}
		if (getlines(fun, p, fp, *argv) == -1) {
			retval = -1;
		}
		fclose(fp);
	}
	return retval;
}

/* increment date */
void
incrdate(struct Date *d)
{
	int n;

	if (d->y < 1 || d->m < 1 || d->m > 12 || d->d < 1 || d->d > daytab[ISLEAP(d->y)][d->m])
		return;
	d->w = (d->w + 1) % DAYSPERWEEK;
	if (d->w == 0) {
		d->pmw++;
		d->nmw++;
	}
	if (d->d < daytab[ISLEAP(d->y)][d->m]) {
		d->d++;
	} else if (d->m < 12) {
		d->m++;
		d->d = 1;
		d->pmw = 1;
		d->nmw = -1 -(getweeknum(daytab[ISLEAP(d->y)][d->m] + 1, d->w - d->d + daytab[ISLEAP(d->y)][d->m] + 1 + MAX_DAYS) - d->pmw);
	} else {
		printf("ASDA\n");
		d->y++;
		d->m = 1;
		d->d = 1;
		d->pmw = 1;
		d->nmw = -1 -(getweeknum(daytab[ISLEAP(d->y)][d->m] + 1, d->w - d->d + daytab[ISLEAP(d->y)][d->m] + 1 + MAX_DAYS) - d->pmw);
	}
	n = daytab[ISLEAP(d->y)][d->m];
}

/* convert string value to int between min and max; exit on error */
int
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
