/* day or day pattern */
struct Date {
	int y;                  /* year */
	int m;                  /* month */
	int d;                  /* month day */
	int w;                  /* weekday */
	int pmw;                /* positive week of the month */
	int nmw;                /* negative week of the month */
};

enum {
	SUNDAY    = 0,
	MONDAY    = 1,
	TUESDAY   = 2,
	WEDNESDAY = 3,
	THURSDAY  = 4,
	FRIDAY    = 5,
	SATURDAY  = 6,
};

typedef int (*Parser)(void *, char *, char *);

void *emalloc(size_t size);
void *ecalloc(size_t nmemb, size_t size);
void incrdate(struct Date *d);
int gettoday(struct Date *);
int datetojulian(struct Date *d);
int readinput(Parser fun, void *p, int argc, char *argv[]);
int strtodate(struct Date *d, const char *s, const char **endptr);
int strtonum(const char *s, int min, int max);
char *estrdup(const char *s);
