/*
** shared memory control utility
*/

#ifndef SHMEM_H
#define SHMEM_H

#include <sys/types.h>

typedef struct {
    caddr_t addr;	/* attached shared memory address */
    size_t  size;	/* size of the shared memory */
    int     shmid;	/* shared memory segment id */
    int     semap;	/* semaphore id */
    int     locktype;	/* current lock type */
} smcd_t;

int smcGetExclusiveLock(smcd_t *_this);
int smcGetSharedLock(smcd_t *_this);
int smcReleaseSharedLock(smcd_t *_this);
int smcReleaseExclusiveLock(smcd_t *_this);
smcd_t* smcGetShmemBuffer(const char *name, int mapSize);
smcd_t* smcCreateShmemBuffer(const char *name, int mapSize);
void smcClose( smcd_t *_this );

#endif	/* SHMEM_H */
