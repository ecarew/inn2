/* Quick and Dirty Hack to reset a CNFS buffer without having to DD the
 * Entire Thing from /dev/zero again. */
#include "portable/system.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <array>

/* Zero the bitfield of one CNFS buffer.  Returns false on any error. */
static bool
reset_buffer(const char *path)
{
    int fd = open(path, O_RDWR, 0664);
    if (fd < 0) {
        fprintf(stderr, "Could not open file %s: %s\n", path,
                strerror(errno));
        return false;
    }

    bool ok = true;
    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "Could not stat file %s: %s\n", path, strerror(errno));
        ok = false;
    } else {
        std::array<char, 512> buf{};

        /* Each bit in the bitfield is 512 bytes of data.  Each byte has 8
         * bits, so calculate as 512 * 8 bytes of data, plus fuzz.  buf has
         * 512 bytes in it, therefore containing data for (512 * 8) * 512
         * bytes of data. */
        size_t numwr = (st.st_size / (512 * 8) / buf.size()) + 50;
        printf("File %s: %lu %lu\n", path, (unsigned long) st.st_size,
               (unsigned long) numwr);
        for (size_t j = 0; j < numwr; j++) {
            if (j % 100 == 0)
                printf("\t%lu/%lu\n", (unsigned long) j,
                       (unsigned long) numwr);
            if (write(fd, buf.data(), buf.size())
                < static_cast<ssize_t>(buf.size())) {
                fprintf(stderr, "Could not write to file %s: %s\n", path,
                        strerror(errno));
                ok = false;
                break;
            }
        }
    }
    close(fd);
    return ok;
}

int
main(int argc, char *argv[])
{
    /* A failure on any file is reported in the exit status, even when a
     * later file succeeds. */
    bool status = true;

    for (int i = 1; i < argc; i++)
        if (!reset_buffer(argv[i]))
            status = false;
    return status ? 0 : 1;
}
