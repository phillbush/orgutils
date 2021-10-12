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

#define DEFDAYS       8                 /* default days til deadline for tasks without deadline */
#define NHASH         128               /* size of hash table */
#define MULTIPLIER    31                /* multiplier for hash table */
#define NCOLS         10                /* number of collumns reserved for task name in long format */
#define TODO          "TODO"
#define DONE          "DONE"
#define PROP_DEPS     "deps"
#define PROP_DUE      "due"

/* task structure */
struct Task {
	struct Task *hnext;             /* pointer for hash table linked list */
	struct Task *next;              /* pointer for linked list */
	struct Edge *deps;              /* linked list of dependency edges */
	time_t due;                     /* due date, at 12:00 */
	int init;                       /* whether task was initialized */
	int pri;                        /* priority */
	int done;                       /* whether task is marked as done */
	char *date;                     /* due date, in format YYYY-MM-DD*/
	char *name;                     /* task name */
	char *desc;                     /* task description */
};

/* dependency link */
struct Edge {
	struct Edge *next;              /* next edge on linked list */
	struct Task *to;                /* task the edge links to */
};

/* list and table of tasks */
struct Agenda {
	struct Task **htab;             /* hash table of tasks */
	struct Task **array;            /* array of sorted, unblocked tasks */
	struct Task *list;              /* head of list of tasks */
	size_t nunblock;                /* number of unblocked tasks */
	size_t ntasks;                  /* number of tasks */
};

/* time for today, 12:00 */
static time_t today;

/* option flags */
static int dflag;                       /* whether to consider tasks with passed deadline as done */
static int lflag;                       /* whether to display tasks in long format */

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: todo [-ld] [-t yyyymmdd] [file...]\n");
	exit(1);
}

/* get time for today, at 12:00 */
static time_t
gettoday(void)
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
	if ((t = mktime(&tm)) == -1)
		err(1, NULL);
	return t;
}

/* compute hash value of string */
static size_t
hash(const char *s)
{
	size_t h;
	unsigned char *p;

	h = 0;
	for (p = (unsigned char *)s; *p != '\0'; p++)
		h = MULTIPLIER * h + *p;
	return h % NHASH;
}

/* find name in agenda, creating if does not exist */
static struct Task *
lookupcreate(struct Agenda *agenda, const char *name)
{
	size_t h;
	struct Task *p;

	h = hash(name);
	for (p = agenda->htab[h]; p != NULL; p = p->hnext)
		if (strcmp(name, p->name) == 0)
			return p;
	p = emalloc(sizeof(*p));
	p->name = estrdup(name);
	p->hnext = agenda->htab[h];
	p->next = agenda->list;
	agenda->htab[h] = p;
	agenda->list = p;
	agenda->ntasks++;
	return p;
}

/* add dependencies to task; we change s */
static void
adddeps(struct Agenda *agenda, struct Task *task, char *s)
{
	struct Task *tmp;
	struct Edge *edge;
	char *t;

	for (t = strtok(s, ","); t != NULL; t = strtok(NULL, ",")) {
		tmp = lookupcreate(agenda, t);
		edge = emalloc(sizeof(*edge));
		edge->next = task->deps;
		edge->to = tmp;
		task->deps = edge;
	}
}

/* add deadline to task */
static void
adddue(struct Task *task, char *s)
{
	struct tm *tmorig;
	struct tm tm;
	time_t t;
	char *ep;

	if ((tmorig = localtime(&today)) == NULL) {
		warn(NULL);
		return;
	}
	tm = *tmorig;
	ep = strptime(s, "%Y-%m-%d", &tm);
	if (ep == NULL || *ep != '\0') {
		errno = EINVAL;
		warnx("invalid date: \"%s\"", s);
		return;
	}
	tm.tm_hour = MIDDAY;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	tm.tm_isdst = -1;
	if ((t = mktime(&tm)) == -1) {
		warn(NULL);
		return;
	}
	task->date = estrdup(s);
	task->due = t;
	if (dflag && task->due < today) {
		task->done = 1;
	}
}

/* parse s for a new task and add it into agenda; we change s */
static void
addtask(struct Agenda *agenda, char *s)
{
	struct Task *task;
	size_t len;
	int done;
	char *name, *prop, *val;
	char *t, *end, *colon;
	int pri;

	/* get status */
	while (isspace(*(unsigned char *)s))
		s++;
	done = 0;
	if (strncmp(s, TODO, sizeof(TODO) - 1) == 0) {
		s += sizeof(TODO) - 1;
	} else if (strncmp(s, DONE, sizeof(DONE) - 1) == 0) {
		done = 1;
		s += sizeof(DONE) - 1;
	}

	/* get name and create task */
	while (isspace(*(unsigned char *)s))
		s++;
	name = NULL;
	for (t = s; *t != '\0' && !isspace(*(unsigned char *)t); t++) {
		if (*t == ':') {
			name = s;
			*t = '\0';
			s = t + 1;
			break;
		}
	}
	if (name == NULL) {
		warnx("not a task: \"%s\"", s);
		return;
	}
	task = lookupcreate(agenda, name);

	/* get priority */
	while (isspace(*(unsigned char *)s))
		s++;
	pri = 0;
	if (s[0] == '(' && s[1] >= 'A' && s[1] <= 'C' && s[2] == ')') {
		switch (s[1]) {
		case 'A':
			pri = +1;
			break;
		default:
			pri = 0;
			break;
		case 'C':
			pri = -1;
			break;
		}
		s += 3;
	}

	/* get properties */
	while (isspace(*(unsigned char *)s))
		s++;
	len = strlen(s);
	for (t = &s[len - 1]; t >= s; t--) {
		colon = NULL;
		while (t >= s && isspace(*(unsigned char *)t))
			t--;
		end = t + 1;
		while (t >= s && !isspace(*(unsigned char *)t)) {
			if (*t == ':') {
				colon = t;
				*colon = '\0';
			}
			t--;
		}
		if (colon) {
			*t = '\0';
			*end = '\0';
			prop = t + 1;
			val = colon + 1;
			if (strcmp(prop, PROP_DUE) == 0) {
				adddue(task, val);
			} else if (strcmp(prop, PROP_DEPS) == 0) {
				adddeps(agenda, task, val);
			} else {
				warnx("unknown property \"%s\"", prop);
			}
		} else {
			break;
		}
	}

	/* get description */
	len = strlen(s);
	for (t = &s[len - 1]; isspace(*(unsigned char *)t) && t >= s; t--)
		*t = '\0';

	free(task->desc);               /* in case we are overriding an existing task */
	task->desc = estrdup(s);
	task->init = 1;
	task->pri = pri;
	task->done = done;
}

/* read tasks from fp into agenda */
static void
readtasks(FILE *fp, struct Agenda *agenda, char *filename, int *exitval)
{
	size_t spn, size;
	char buf[BUFSIZ];

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (*buf == '#')
			continue;
		size = 0;
		while ((spn = strcspn(buf + size, "\n")) > 1 && buf[size + spn - 1] == '\\') {
			buf[size + spn - 1] = ' ';
			size += spn;
			fgets(buf + size, sizeof(buf) - size, fp);
		}
		addtask(agenda, buf);
	}
	if (ferror(fp)) {
		warn("%s", filename);
		*exitval = 1;
		clearerr(fp);
	}
}

/* compare tasks */
static int
comparetask(const void *a, const void *b)
{
	struct Task *taska, *taskb;
	time_t timea, timeb;
	time_t tmpa, tmpb;

	taska = *(struct Task **)a;
	taskb = *(struct Task **)b;
	tmpa = (taska->due != 0) ? difftime(taska->due, today) / SECS_PER_DAY - 1 : DEFDAYS;
	tmpb = (taskb->due != 0) ? difftime(taskb->due, today) / SECS_PER_DAY - 1 : DEFDAYS;
	timea = timeb = 0;
	if (tmpa < 0) {
		tmpa = -tmpa;
		while (tmpa >>= 1) {
			--timea;
		}
	} else {
		while (tmpa >>= 1) {
			++timea;
		}
	}
	if (tmpb < 0) {
		tmpb = -tmpb;
		while (tmpb >>= 1) {
			--timeb;
		}
	} else {
		while (tmpb >>= 1) {
			++timeb;
		}
	}
	timea -= taska->pri;
	timeb -= taskb->pri;
	if (timea < timeb)
		return -1;
	if (timea > timeb)
		return +1;
	return 0;
}

/* sort tasks in agenda */
static void
sorttasks(struct Agenda *agenda)
{
	struct Task *task;
	struct Edge *edge;
	int cont;

	agenda->array = ecalloc(agenda->ntasks, sizeof(*agenda->array));
	for (task = agenda->list; task != NULL; task = task->next) {
		if (!task->init)
			errx(1, "task \"%s\" mentioned but not defined", task->name);
		if (task->done)
			continue;
		if (task->deps != NULL) {
			cont = 0;
			for (edge = task->deps; edge != NULL; edge = edge->next) {
				if (!edge->to->done) {
					cont = 1;
					break;
				}
			}
			if (cont) {
				continue;
			}
		}
		agenda->array[agenda->nunblock++] = task;
	}
	qsort(agenda->array, agenda->nunblock, sizeof(*agenda->array), comparetask);
}

/* print sorted tasks */
static void
printtasks(struct Agenda *agenda)
{
	struct Task *task;
	size_t i;
	int n;

	for (i = 0; i < agenda->nunblock; i++) {
		task = agenda->array[i];
		if (lflag) {
			if ((n = printf("%s", task->name)) < 0)
				break;
			n = (n > 0 && n < NCOLS) ? NCOLS - n : 0;
			if (printf(":%*c (%c) %s", n, ' ',
			             (task->pri < 0 ? 'C' : (task->pri > 0 ? 'A' : 'B')),
			             task->desc) < 0)
				break;
			if (task->date != NULL) {
				if (printf(" due:%s", task->date) < 0) {
					break;
				}
			}
		} else {
			if (printf("%s", task->desc) < 0)
				break;
		}
		if (printf("\n") < 0) {
			break;
		}
	}
	if (ferror(stdout)) {
		err(1, "stdout");
	}
}

/* free agenda and its tasks */
static void
freeagenda(struct Agenda *agenda)
{
	struct Task *task, *ttmp;
	struct Edge *edge, *etmp;

	for (task = agenda->list; task != NULL; ) {
		for (edge = task->deps; edge != NULL; ) {
			etmp = edge;
			edge = edge->next;
			free(etmp);
		}
		ttmp = task;
		task = task->next;
		free(ttmp->name);
		free(ttmp->desc);
		free(ttmp->date);
		free(ttmp);
	}
	free(agenda->array);
	free(agenda);
}

/* todo: print next tasks */
int
main(int argc, char *argv[])
{
	static struct Agenda *agenda;
	FILE *fp;
	int exitval, ch;

	today = gettoday();
	while ((ch = getopt(argc, argv, "dlt:")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'l':
			lflag = 1;
			break;
		case 't':
			today = strtotime(optarg);
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	exitval = 0;
	agenda = ecalloc(1, sizeof(*agenda));
	agenda->htab = ecalloc(NHASH, sizeof(*(agenda->htab)));
	if (argc == 0) {
		readtasks(stdin, agenda, "stdin", &exitval);
	} else {
		for (; *argv != NULL; argv++) {
			if (strcmp(*argv, "-") == 0) {
				readtasks(stdin, agenda, "stdin", &exitval);
				continue;
			}
			if ((fp = fopen(*argv, "r")) == NULL) {
				warn("%s", *argv);
				exitval = 1;
				continue;
			}
			readtasks(fp, agenda, *argv, &exitval);
			fclose(fp);
		}
	}
	free(agenda->htab);
	sorttasks(agenda);
	printtasks(agenda);
	freeagenda(agenda);
	return exitval;
}
