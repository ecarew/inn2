/*
** Shared memory control utility.
*/

#ifndef SHMEM_H
#define SHMEM_H

#include <sys/types.h>

BEGIN_DECLS

typedef struct {
    caddr_t addr; /* attached shared memory address */
    size_t size;  /* size of the shared memory */
    int shmid;    /* shared memory segment id */
    int semap;    /* semaphore id */
    int locktype; /* current lock type */
} smcd_t;

int smcGetExclusiveLock(smcd_t *smc);
int smcGetSharedLock(smcd_t *smc);
int smcReleaseSharedLock(smcd_t *smc);
int smcReleaseExclusiveLock(smcd_t *smc);
smcd_t *smcGetShmemBuffer(const char *name, int mapSize);
smcd_t *smcCreateShmemBuffer(const char *name, int mapSize);
void smcClose(smcd_t *smc);

END_DECLS

#endif /* SHMEM_H */
