#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#define DEFDAYS       8                 /* default days til deadline for tasks without deadline */
#define DEFNICE       3                 /* log2(DEFDAYS) */
#define NHASH         128               /* size of hash table */
#define MULTIPLIER    31                /* multiplier for hash table */
#define TODO          "TODO"
#define DONE          "DONE"
#define PROP_DEPS     "deps"
#define PROP_DUE      "due"

/* collection of tasks */
struct Agenda {
	/*
	 * We collect tasks into five different data structures.
	 * - (1) A hash table.
	 * - (2) An unsorted singly linked list.
	 * - (3) A directed acyclic graph.
	 * - (4) A topologically sorted doubly linked list.
	 * - (5) A sorted array.
	 *
	 * .The reading phase.
	 * First, we collect tasks in a hash table (1st data structure)
	 * and an unsorted singly linked list (2nd).  While we are
	 * collecting tasks, we get their dependencies and build a
	 * directed acyclic graph of tasks (3rd).  After reading all
	 * tasks, we free the hash table (it is only used to get the
	 * dependencies without having to loop over the unsorted list
	 * all the time).
	 *
	 * .The sorting phase.
	 * After collecting tasks, we iterate over the unsorted list of
	 * tasks and visit each node in the directed graph and create a
	 * topologically sorted doubly linked list of tasks (4th), that
	 * will be read in the reverse topological order to compute the
	 * niceness (anti-urgency) of each task.  Then, we iterate over
	 * the sorted list to extract those tasks that are not blocked
	 * by a open (not done) task into an array of tasks (5th) that
	 * will be sorted based on the niceness of the tasks.  This
	 * array contains only those tasks that are unblocked.
	 *
	 * .The writing phase.
	 * Finally, we loop through the array of tasks to print each
	 * task to the standard output.
	 */

	struct Task **htab;             /* hash table of tasks */
	struct Task **array;            /* array of pointers to sorted, unblocked tasks */
	struct Task *unsort;            /* head of unsorted list of tasks */
	struct Task *shead, *stail;     /* head and tail of sorted list of tasks */
	size_t nunblock;                /* number of unblocked tasks */
	size_t ntasks;                  /* number of tasks */
};

/* task structure */
struct Task {
	/*
	 * A task maintains some pointers for the data structures where
	 * tasks are organized.  See the comment at struct Agenda for
	 * more information.
	 */
	struct Task *hnext;             /* pointer for hash table linked list */
	struct Task *unext;             /* pointer for unsorted linked list */
	struct Task *sprev, *snext;     /* pointer for sorted linked list */
	struct Edge *deps;              /* linked list of dependency edges */

	/*
	 * Tasks are first read from the files (or stdin) and collected.
	 * We use a hash table to lookup tasks or create them.  When a
	 * task is read, it is created and initialized (its init field
	 * is set as 1).  When a task is only mentioned as a dependency
	 * of another task, its init field is zero.
	 */
	int init;                       /* whether task was initialized */

	/*
	 * The niceness of a task is its anti-urgency.  The nicer a task
	 * is, the less urgent it is.   The nice field is computed after
	 * generating a topologically sorted doubly linked list of
	 * tasks.  We need this topological order because the niceness
	 * of a task depends on the niceness of the tasks that depends
	 * on it.
	 *
	 * The niceness of a task is the log2 of the days from now until
	 * its deadline, minus the priority.
	 *
	 * Tasks without a deadline are considered to be due in eight
	 * days (the power of two that is more close to the duration of
	 * a week in days).  Tasks without a priority have priority of
	 * zero.  So, by default, the niceness of a regular task without
	 * dependencies is 3 (log2(8)-0).
	 */
	int nice;                       /* task niceness; the lower the more urgent */

	/*
	 * For topologically sorting the tasks, we need to know whether
	 * a task was visited.
	 */
	int visited;                    /* whether node was visited while sorting */

	/*
	 * The deadline of the task is represented by the date in UNIX
	 * julian day (number of days since UNIX epoch).  The number
	 * of days from today until this deadline is stored in the
	 * ndays field.  The priority of a task, represented by the
	 * pri field, can be -1, 0, or +1.
	 *
	 * The ndays and the priority of a task are computed from the
	 * input information, but can be modified at runtime.  The ndays
	 * field can be inherited from the dependents as their value of
	 * ndays minus one.  The pri field of a task is the larger value
	 * between its current value and the pri of a dependent.
	 */
	int due;                        /* due date in UNIX julian day */
	int ndays;                      /* due date - today */
	int pri;                        /* priority */
	int done;                       /* whether task is marked as done */

	/*
	 * Tasks are identified by the following fields.
	 */
	char *name;                     /* task name */
	const char *filename;           /* file task came from */

	/*
	 * The following fields are only used for printing the task.
	 */
	char *date;                     /* due date, in format YYYY-MM-DD*/
	char *desc;                     /* task description */
};

/* dependency link for the directed graph */
struct Edge {
	struct Edge *next;              /* next edge on linked list */
	struct Task *to;                /* task the edge links to */
};

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: todo [-dl] [-T yyyy-mm-dd] [file...]\n");
	exit(1);
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
lookupcreate(struct Agenda *agenda, const char *filename, const char *name)
{
	struct Task *task;
	size_t h;

	h = hash(name);
	for (task = agenda->htab[h]; task != NULL; task = task->hnext)
		if (strcmp(name, task->name) == 0 && task->filename == filename)
			return task;
	task = emalloc(sizeof(*task));
	task->name = estrdup(name);
	task->filename = filename;
	task->hnext = agenda->htab[h];
	task->unext = agenda->unsort;
	agenda->htab[h] = task;
	agenda->unsort = task;
	agenda->ntasks++;
	return task;
}

/* add dependencies to task; we change s */
static void
adddeps(struct Agenda *agenda, struct Task *task, char *filename, char *s)
{
	struct Task *tmp;
	struct Edge *edge;
	char *t;

	for (t = strtok(s, ","); t != NULL; t = strtok(NULL, ",")) {
		tmp = lookupcreate(agenda, filename, t);
		edge = emalloc(sizeof(*edge));
		edge->next = task->deps;
		edge->to = tmp;
		task->deps = edge;
	}
}

/* parse line for a new task and add it into agenda; we change line; return -1 on error */
static int
parseline(void *p, char *line, char *filename)
{
	struct Agenda *agenda = p;
	struct Date d;
	struct Task *task;
	size_t len;
	int done;
	char *name, *prop, *val;
	char *s, *end, *colon;
	int pri;

	/* get status */
	while (isspace(*(unsigned char *)line))
		line++;
	done = 0;
	if (strncmp(line, TODO, sizeof(TODO) - 1) == 0) {
		line += sizeof(TODO) - 1;
	} else if (strncmp(line, DONE, sizeof(DONE) - 1) == 0) {
		done = 1;
		line += sizeof(DONE) - 1;
	}

	/* get name and create task */
	while (isspace(*(unsigned char *)line))
		line++;
	name = NULL;
	for (s = line; *s != '\0' && !isspace(*(unsigned char *)s); s++) {
		if (*s == ':') {
			name = line;
			*s = '\0';
			line = s + 1;
			break;
		}
	}
	if (name == NULL)
		return - 1;
	task = lookupcreate(agenda, filename, name);

	/* get priority */
	while (isspace(*(unsigned char *)line))
		line++;
	pri = 0;
	if (line[0] == '(' && line[1] >= 'A' && line[1] <= 'C' && line[2] == ')') {
		switch (line[1]) {
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
		line += 3;
	}

	/* get properties */
	while (isspace(*(unsigned char *)line))
		line++;
	len = strlen(line);
	for (s = &line[len - 1]; s >= line; s--) {
		colon = NULL;
		while (s >= line && isspace(*(unsigned char *)s))
			s--;
		end = s + 1;
		while (s >= line && !isspace(*(unsigned char *)s)) {
			if (*s == ':') {
				colon = s;
				*colon = '\0';
			}
			s--;
		}
		if (colon) {
			*s = '\0';
			*end = '\0';
			prop = s + 1;
			val = colon + 1;
			if (strcmp(prop, PROP_DUE) == 0) {
				if (strtodate(&d, val, NULL) == -1) {
					warnx("improper time format: %s", val);
				} else {
					task->date = estrdup(val);
					task->due = datetojulian(&d);
				}
			} else if (strcmp(prop, PROP_DEPS) == 0) {
				adddeps(agenda, task, filename, val);
			} else {
				warnx("unknown property \"%s\"", prop);
			}
		} else {
			break;
		}
	}

	/* get description */
	len = strlen(line);
	for (s = &line[len - 1]; isspace(*(unsigned char *)s) && s >= line; s--)
		*s = '\0';

	free(task->desc);               /* in case we are overriding an existing task */
	task->desc = estrdup(line);
	task->init = 1;
	task->pri = pri;
	task->visited = 0;
	task->nice = DEFNICE;
	task->done = done;
	return 0;
}

/* visit task and their dependencies */
static void
visittask(struct Agenda *agenda, struct Task *task)
{
	struct Edge *edge;

	if (task->visited > 1)
		return;
	if (task->visited == 1)
		errx(1, "%s: cyclic dependency between tasks", task->filename);
	task->visited = 1;
	for (edge = task->deps; edge != NULL; edge = edge->next)
		visittask(agenda, edge->to);
	task->visited = 2;
	if (agenda->shead == NULL)
		agenda->shead = task;
	if (agenda->stail != NULL)
		agenda->stail->snext = task;
	task->sprev = agenda->stail;
	agenda->stail = task;
}

/* compute niceness as log2(due - today - sub) - pri */
static int
calcnice(int ndays, int pri)
{
	int nice;

	nice = 0;
	if (ndays < 0) {
		ndays = -ndays;
		while (ndays >>= 1) {
			--nice;
		}
	} else {
		while (ndays >>= 1) {
			++nice;
		}
	}
	return nice - pri;
}

/* compare the niceness of two tasks; used by qsort(3) */
static int
comparetask(const void *a, const void *b)
{
	struct Task *taska, *taskb;

	taska = *(struct Task **)a;
	taskb = *(struct Task **)b;
	if (taska->nice < taskb->nice)
		return -1;
	if (taska->nice > taskb->nice)
		return +1;
	return 0;
}

/* compute task niceness; create array of unblocked tasks; and sort it based on niceness */
static void
sorttasks(struct Agenda *agenda, int today, int dflag)
{
	struct Task *task;
	struct Edge *edge;
	int cont;

	/* first pass: topological sort (also compute ndays and check if task was not initialized) */
	for (task = agenda->unsort; task != NULL; task = task->unext) {
		if (!task->init) {
			errx(1, "task \"%s\" mentioned but not defined", task->name);
		}
		if ((task->ndays = (task->due > 0) ? task->due - today : DEFDAYS) < 0 && dflag) {
			task->done = 1;
		}
		if (!task->visited) {
			visittask(agenda, task);
		}
	}

	/* second pass: compute nicenesses; and reset priority and ndays of dependencies if necessary */
	for (task = agenda->stail; task != NULL; task = task->sprev) {
		task->nice = calcnice(task->ndays, task->pri);
		for (edge = task->deps; edge != NULL; edge = edge->next) {
			if (task->due != 0) {
				if (edge->to->due == 0 || task->ndays <= edge->to->ndays) {
					edge->to->ndays = task->ndays - 1;
				}
				edge->to->due = 1;
			}
			if (task->pri > edge->to->pri) {
				edge->to->pri = task->pri;
			}
		}
	}

	/* third pass: create array of unblocked tasks */
	agenda->array = ecalloc(agenda->ntasks, sizeof(*agenda->array));
	for (task = agenda->shead; task != NULL; task = task->snext) {
		if (task->done) {
			continue;
		}
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

	/* fourth pass: sort array of unblocked tasks based on niceness */
	qsort(agenda->array, agenda->nunblock, sizeof(*agenda->array), comparetask);
}

/* print sorted tasks */
static void
printtasks(struct Agenda *agenda, int lflag, int prefix)
{
	struct Task *task;
	size_t i;

	for (i = 0; i < agenda->nunblock; i++) {
		task = agenda->array[i];
		if (lflag)
			printf("(%c) ", (task->pri < 0 ? 'C' : (task->pri > 0 ? 'A' : 'B')));
		if (lflag && prefix)
			printf("%s: ", task->filename);
		printf("%s", task->desc);
		if (lflag && task->date != NULL)
			printf(" due:%s", task->date);
		printf("\n");
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
}

/* todo: print next tasks */
int
main(int argc, char *argv[])
{
	static struct Agenda agenda = {
		.array = NULL,
		.unsort = NULL,
		.shead = NULL,
		.stail = NULL,
		.nunblock = 0,
		.ntasks = 0,
	};
	struct Date d;
	int exitval = 0;
	static int dflag = 0;           /* whether to consider tasks with passed deadline as done */
	static int lflag = 0;           /* whether to display tasks in long format */
	int today;                      /* today in UNIX julian day */
	int ch;

	if (gettoday(&d) == -1)
		err(1, NULL);
	today = datetojulian(&d);
	agenda.htab = ecalloc(NHASH, sizeof(*agenda.htab));
	while ((ch = getopt(argc, argv, "dlT:")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'T':
			if (strtodate(&d, optarg, NULL) == -1)
				errx(1, "improper argument date: %s", optarg);
			today = datetojulian(&d);
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (readinput(parseline, &agenda, argc, argv) == -1)
		exitval = 1;
	free(agenda.htab);              /* we don't need the hash table anymore */
	sorttasks(&agenda, today, dflag);
	printtasks(&agenda, lflag, argc > 1);
	freeagenda(&agenda);
	return exitval;
}
