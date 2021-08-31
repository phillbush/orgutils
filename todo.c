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
#define DEFDAYS       8                 /* default days til deadline for tasks without deadline */
#define MIDDAY        12                /* 12:00 */
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
	struct Task *unext;             /* pointer for unsorted linked list */
	struct Task *snext;             /* pointer for sorted linked list */
	struct Edge *deps;              /* linked list of dependency edges */
	time_t due;                     /* due date, at 12:00 */
	int pri;                        /* priority */
	int visited;                    /* whether node was visited while sorting */
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
	struct Task *unsort;            /* head of unsorted list of tasks */
	struct Task *shead, *stail;     /* head and tail of sorted list of tasks */
	size_t nunblock;                /* number of unblocked tasks */
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
	(void)fprintf(stderr, "usage: todo [-ld] [file...]\n");
	exit(1);
}

/* call malloc checking for error */
static void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
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
lookup(struct Agenda *agenda, const char *name)
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
	p->unext = agenda->unsort;
	agenda->htab[h] = p;
	agenda->unsort = p;
	return p;
}

/* create agenda and hash table */
static struct Agenda *
newagenda(void)
{
	struct Agenda *agenda;

	agenda = ecalloc(1, sizeof(*agenda));
	agenda->htab = ecalloc(NHASH, sizeof(*(agenda->htab)));
	return agenda;
}

/* add dependencies to task; we change s */
static void
adddeps(struct Agenda *agenda, struct Task *task, char *s)
{
	struct Task *tmp;
	struct Edge *edge;
	char *t;

	for (t = strtok(s, ","); t != NULL; t = strtok(NULL, ",")) {
		tmp = lookup(agenda, t);
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
		warn("%s", s);
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

	while (isspace(*(unsigned char *)s))
		s++;
	done = 0;
	if (strncmp(s, TODO, sizeof(TODO) - 1) == 0) {
		s += sizeof(TODO) - 1;
	} else if (strncmp(s, DONE, sizeof(DONE) - 1) == 0) {
		done = 1;
		s += sizeof(DONE) - 1;
	}
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
	if (name == NULL)
		return;
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
	task = lookup(agenda, name);
	task->pri = pri;
	task->done = done;
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
	len = strlen(s);
	for (t = &s[len - 1]; isspace(*(unsigned char *)t) && t >= s; t--)
		*t = '\0';
	free(task->desc);
	task->desc = estrdup(s);
}

/* read tasks from fp into agenda */
static void
readtasks(FILE *fp, struct Agenda *agenda)
{
	char buf[BUFSIZ];

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		addtask(agenda, buf);
	}
}

/* visit task and their dependencies */
static void
visittask(struct Agenda *agenda, struct Task *task)
{
	struct Edge *edge;

	if (task->visited > 1)
		return;
	if (task->visited == 1)
		errx(1, "cyclic dependency between tasks");
	task->visited = 1;
	for (edge = task->deps; edge != NULL; edge = edge->next)
		visittask(agenda, edge->to);
	task->visited = 2;
	if (agenda->shead == NULL)
		agenda->shead = task;
	if (agenda->stail != NULL)
		agenda->stail->snext = task;
	agenda->stail = task;
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
	tmpa = (taska->due != 0) ? (taska->due - today) / SECS_PER_DAY : DEFDAYS;
	tmpb = (taskb->due != 0) ? (taskb->due - today) / SECS_PER_DAY : DEFDAYS;
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

/* perform topological sort on agenda */
static void
sorttasks(struct Agenda *agenda)
{
	struct Task *task;
	struct Edge *edge;
	size_t ntasks;
	int cont;

	free(agenda->htab);
	ntasks = 0;
	for (task = agenda->unsort; task != NULL; task = task->unext) {
		if (!task->visited)
			visittask(agenda, task);
		ntasks++;
	}
	agenda->array = ecalloc(ntasks, sizeof(*agenda->array));
	for (task = agenda->shead; task != NULL; task = task->snext) {
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
			n = printf("%s", task->name);
			n = (n > 0 && n < NCOLS) ? NCOLS - n : 0;
			(void)printf(":%*c (%c) %s", n, ' ',
			             (task->pri < 0 ? 'C' : (task->pri > 0 ? 'A' : 'B')),
			             task->desc);
			if (task->date != NULL) {
				(void)printf(" due:%s", task->date);
			}
		} else {
			(void)printf("%s", task->desc);
		}
		printf("\n");
	}
}

/* free agenda and its tasks */
static void
freeagenda(struct Agenda *agenda)
{
	struct Task *task, *ttmp;
	struct Edge *edge, *etmp;

	for (task = agenda->unsort; task != NULL; ) {
		for (edge = task->deps; edge != NULL; ) {
			etmp = edge;
			edge = edge->next;
			free(etmp);
		}
		ttmp = task;
		task = task->unext;
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
	while ((ch = getopt(argc, argv, "dl")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'l':
			lflag = 1;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	exitval = 0;
	agenda = newagenda();
	if (argc == 0) {
		readtasks(stdin, agenda);
	} else {
		for (; *argv != NULL; argv++) {
			if (strcmp(*argv, "-") == 0) {
				readtasks(stdin, agenda);
				continue;
			}
			if ((fp = fopen(*argv, "r")) == NULL) {
				warn("%s", *argv);
				exitval = 1;
				continue;
			}
			readtasks(fp, agenda);
			fclose(fp);
		}
	}
	sorttasks(agenda);
	printtasks(agenda);
	freeagenda(agenda);
	return exitval;
}
