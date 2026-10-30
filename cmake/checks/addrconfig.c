/* _INN_FUNC_GETADDRINFO_ADDRCONFIG_SOURCE from m4/socket.m4,
   run by cmake/InnConfigure.cmake. */
#define HAVE_UNISTD_H 1
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

int
main(void) {
    struct addrinfo hints, *ai;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;
    return (getaddrinfo("localhost", NULL, &hints, &ai) != 0);
}
