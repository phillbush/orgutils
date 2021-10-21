#define SECS_PER_DAY  ((time_t)(24 * 60 * 60))
#define MIDDAY        12                /* 12:00 */

void *emalloc(size_t size);
void *ecalloc(size_t nmemb, size_t size);
char *estrdup(const char *s);
char *getfullname(const char *prefix, const char *name);
int strtonum(const char *s, int min, int max);
time_t strtotime(const char *s);
