/* _INN_FUNC_MMAP_SOURCE from m4/mmap.m4,
   run by cmake/InnConfigure.cmake. */
#define HAVE_UNISTD_H 1
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

int
main()
{
    int *data, *data2;
    int i, fd;

    /* First, make a file with some known garbage in it.  Use something
       larger than one page but still an odd page size. */
    data = malloc(20000);
    if (!data)
        return 1;
    for (i = 0; i < 20000 / sizeof(int); i++)
        data[i] = rand();
    umask(0);
    fd = creat("conftestmmaps", 0600);
    if (fd < 0)
        return 1;
    if (write(fd, data, 20000) != 20000)
        return 1;
    close(fd);

    /* Next, try to mmap the file and make sure we see the same garbage. */
    fd = open("conftestmmaps", O_RDWR);
    if (fd < 0)
        return 1;
    data2 = mmap(0, 20000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data2 == (int *) -1)
        return 1;
    for (i = 0; i < 20000 / sizeof(int); i++)
        if (data[i] != data2[i])
            return 1;

    close(fd);
    unlink("conftestmmaps");
    return 0;
}
