#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "util.h"

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

/* add prefix to event name if necessary; return full name in allocated string */
char *
getfullname(const char *prefix, const char *name)
{
	size_t size;
	char *fullname;

	if (prefix != NULL) {
		size = strlen(prefix) + strlen(name) + 3;       /* 3 = strlen(": ") + 1 for '\0' */
		fullname = emalloc(size);
		snprintf(fullname, size, "%s: %s", prefix, name);
	} else {
		fullname = estrdup(name);
	}
	return fullname;
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

/* convert YYYYMMDD to time */
time_t
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
