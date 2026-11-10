#ifndef OVDB_H
#define OVDB_H

#include "config.h"

struct ov_search;

BEGIN_DECLS

bool ovdb_open(int mode);
bool ovdb_groupstats(const char *group, int *lo, int *hi, int *count,
                     int *flag);
bool ovdb_groupadd(const char *group, ARTNUM lo, ARTNUM hi, char *flag);
bool ovdb_groupdel(const char *group);
bool ovdb_add(const char *group, ARTNUM artnum, TOKEN token, char *data,
              int len, time_t arrived, time_t expires);
bool ovdb_cancel(const char *group, ARTNUM artnum);
struct ov_search *ovdb_opensearch(const char *group, int low, int high);
bool ovdb_search(struct ov_search *handle, ARTNUM *artnum, char **data,
                 int *len, TOKEN *token, time_t *arrived);
void ovdb_closesearch(struct ov_search *handle);
bool ovdb_getartinfo(const char *group, ARTNUM artnum, TOKEN *token);
bool ovdb_expiregroup(const char *group, int *lo, struct history *h);
bool ovdb_space(float *percent);
bool ovdb_sort(OVSORTTYPE *sort);
bool ovdb_cutofflow(bool cutofflow);
bool ovdb_staticsearch(bool *staticsearch);
bool ovdb_cachekeep(bool keep);
bool ovdb_cachefree(void);
void ovdb_close(void);

END_DECLS

#endif /* _OVDB_H_ */
