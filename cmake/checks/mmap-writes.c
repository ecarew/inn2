/* _INN_FUNC_MMAP_SEES_WRITES_SOURCE from m4/mmap.m4,
   run by cmake/InnConfigure.cmake. */
#define HAVE_UNISTD_H 1
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

/* Fractional page is probably worst case. */
static char zbuff[1024];
static char fname[] = "conftestw";

int
main()
{
    char *map;
    int i, fd;

    fd = open(fname, O_RDWR | O_CREAT, 0660);
    if (fd < 0)
        return 1;
    unlink(fname);
    write(fd, zbuff, sizeof(zbuff));
    lseek(fd, 0, SEEK_SET);
    map = mmap(0, sizeof(zbuff), PROT_READ, MAP_SHARED, fd, 0);
    if (map == (char *) -1)
        return 2;
    for (i = 0; fname[i]; i++) {
        if (write(fd, &fname[i], 1) != 1)
            return 3;
        if (map[i] != fname[i])
            return 4;
    }
    return 0;
}
