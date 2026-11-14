/*
**  The client connection's bottom layer: reads and writes on the client
**  socket, plain or TLS, with timeouts (Boost.Asio).
**
**  nnrpd serves one client per process, on descriptors 0 and 1, which are
**  the same socket.  Everything above this layer (the line buffer in
**  line.cc, COMPRESS DEFLATE and the SASL security layer) is unchanged;
**  only the I/O on the socket goes through here.  TLS is an ssl::stream
**  over the socket, made from the SSL_CTX that tls.cc configures.
*/

#ifndef NNRPD_TRANSPORT_H
#define NNRPD_TRANSPORT_H 1

#include "config.h"

#include <sys/types.h>
#include <sys/uio.h>

#ifdef HAVE_OPENSSL
#    include <openssl/ssl.h>
#endif

/* Read at most len bytes from the client, waiting at most timeout seconds
   (no limit if timeout <= 0).  Returns the number of bytes read, 0 at end
   of file, -1 on error (errno set) and -2 on timeout.  Time spent waiting
   counts as TMR_IDLE. */
ssize_t transport_read(void *buf, size_t len, int timeout);

/* Write all of the buffers to the client.  Gives up after the client
   timeout (readers.conf or inn.conf clienttimeout) without progress.
   Returns false on error, with errno set. */
bool transport_write(const struct iovec *vec, int count);
bool transport_write(const char *buf, size_t len);

#ifdef HAVE_OPENSSL
/* The server side of a TLS handshake on the client socket, waiting at most
   timeout seconds.  prepare, if not null, is called with the new SSL
   object before the handshake (for debugging callbacks).  Returns the
   connection's SSL object, owned by the transport, or nullptr if the
   handshake failed or timed out; afterwards, reads and writes go through
   TLS.  Plaintext not yet read stays unread: the caller throws away its
   own buffered plaintext. */
SSL *transport_start_tls(SSL_CTX *ctx, int timeout, void (*prepare)(SSL *));
#endif

/* End the connection's I/O: a TLS close_notify (waiting briefly for the
   client's), then the transport lets go of the socket without closing
   it. */
void transport_close();

#endif /* !NNRPD_TRANSPORT_H */
