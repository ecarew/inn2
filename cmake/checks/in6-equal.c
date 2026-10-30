/* _INN_MACRO_IN6_ARE_ADDR_EQUAL_SOURCE from m4/socket.m4,
   run by cmake/InnConfigure.cmake. */
#define HAVE_UNISTD_H 1
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int
main (void)
{
    struct in6_addr a;
    struct in6_addr b;

    inet_pton(AF_INET6, "fe80::1234:5678:abcd", &a);
    inet_pton(AF_INET6, "fe80::1234:5678:abcd", &b);
    return IN6_ARE_ADDR_EQUAL(&a, &b) ? 0 : 1;
}
