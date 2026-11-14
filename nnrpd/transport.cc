/*
**  The client connection's bottom layer on Boost.Asio (see transport.h).
**
**  Each operation starts one asynchronous read, write, handshake or
**  shutdown, arms a timer, and runs the process's io_context until one of
**  them finishes; a timer that fires first cancels the operation.  nnrpd
**  serves one client per process, so the loop never has more than that
**  one operation and its timer.
**
**  With TLS, the ssl::stream calls SSL_read first and waits on the socket
**  only when OpenSSL needs more input, so data OpenSSL has already
**  decrypted is never left waiting behind a readiness check, and any write
**  OpenSSL needs during a read is done within the same operation.  That
**  replaces the old blocking SSL_read inside alarm(), and lets the idle
**  timeout and the TMR_IDLE statistic work the same with and without TLS.
*/

#include "transport.h"

#include "portable/system.h"

#include <cerrno>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#ifdef HAVE_OPENSSL
#    include <boost/asio/ssl/context.hpp>
#    include <boost/asio/ssl/error.hpp>
#    include <boost/asio/ssl/stream.hpp>
#endif

#include "inn/innconf.h"
#include "inn/timer.h"
#include "nnrpd.h"
#ifdef HAVE_OPENSSL
#    include "tls.h"
#endif

namespace asio = boost::asio;
using boost::system::error_code;

namespace {

struct Transport {
    /* One session per process, one thread: no locking in the scheduler or
       the reactor (it showed in W4T's profile). */
    asio::io_context io{BOOST_ASIO_CONCURRENCY_HINT_UNSAFE};
    /* The client socket, descriptor 0 (1 is the same socket). */
    asio::posix::stream_descriptor sock{io};
    asio::steady_timer timer{io};
#ifdef HAVE_OPENSSL
    /* Set by transport_start_tls; once set, all I/O goes through it, even
       after it has failed (a failed TLS session never falls back to
       plaintext). */
    std::optional<asio::ssl::stream<asio::posix::stream_descriptor &>> tls;
    /* False once the TLS session has failed or timed out: no close_notify
       then. */
    bool tls_ok = false;
    /* transport_write's gather buffer, kept between writes as SSL_writev
       kept its own: a new one per ARTICLE made glibc consolidate its heap
       on every large reply. */
    std::string wbuf;
#endif

    Transport()
    {
        sock.assign(STDIN_FILENO);
    }

    ~Transport()
    {
#ifdef HAVE_OPENSSL
        tls.reset();
#endif
        /* The socket is nnrpd's, not ours: let go without closing. */
        sock.release();
    }

    Transport(const Transport &) = delete;
    Transport &operator=(const Transport &) = delete;
};

/* Created on first use, in the process that serves the client (after any
   fork of the daemon-mode listener). */
std::optional<Transport> transport;

Transport &
tr()
{
    if (!transport)
        transport.emplace();
    return *transport;
}

/* Run one asynchronous operation with a timeout.  start is called with a
   completion handler taking (error_code, size_t).  Returns the operation's
   error, or asio::error::timed_out if the timer fired first; *bytes gets
   the operation's byte count. */
template <class Start>
error_code
run(Start start, int timeout, std::size_t *bytes = nullptr)
{
    Transport &t = tr();
    bool done = false;
    bool timed_out = false;
    error_code result;
    std::size_t n = 0;

    t.io.restart();
    start([&](const error_code &ec, std::size_t count) {
        result = ec;
        n = count;
        done = true;
        t.timer.cancel();
    });
    if (timeout > 0) {
        t.timer.expires_after(std::chrono::seconds(timeout));
        t.timer.async_wait([&](const error_code &ec) {
            if (!ec && !done) {
                timed_out = true;
                error_code ignored;
                t.sock.cancel(ignored);
            }
        });
    }
    t.io.run();
    if (bytes != nullptr)
        *bytes = n;
    return timed_out ? error_code(asio::error::timed_out) : result;
}

/* errno for an Asio error. */
int
errno_for(const error_code &ec)
{
    if (ec == asio::error::timed_out)
        return ETIMEDOUT;
    if (ec.category() == boost::system::system_category())
        return ec.value();
    return ECONNRESET;
}

/* The client timeout for writes. */
int
write_timeout()
{
    return PERMaccessconf != nullptr ? PERMaccessconf->clienttimeout
                                     : static_cast<int>(innconf->clienttimeout);
}

#ifdef HAVE_OPENSSL
/* After a failed TLS operation: no close_notify, and nothing more in TLS
   (tls_conn is how the rest of nnrpd asks whether a TLS session is up). */
void
tls_failed()
{
    Transport &t = tr();
    if (t.tls)
        t.tls_ok = false;
    tls_conn = nullptr;
}
#endif

} // namespace

ssize_t
transport_read(void *buf, size_t len, int timeout)
{
    Transport &t = tr();
    std::size_t n = 0;
    error_code ec;

    TMRstart(TMR_IDLE);
#ifdef HAVE_OPENSSL
    if (t.tls) {
        if (!t.tls_ok) {
            TMRstop(TMR_IDLE);
            errno = ECONNRESET;
            return -1;
        }
        ec = run(
            [&](auto handler) {
                t.tls->async_read_some(asio::buffer(buf, len), handler);
            },
            timeout, &n);
    } else
#endif
        ec = run(
            [&](auto handler) {
                t.sock.async_read_some(asio::buffer(buf, len), handler);
            },
            timeout, &n);
    TMRstop(TMR_IDLE);

    if (!ec)
        return static_cast<ssize_t>(n);
#ifdef HAVE_OPENSSL
    if (t.tls)
        tls_failed();
#endif
    if (ec == asio::error::timed_out)
        return -2;
    if (ec == asio::error::eof)
        return 0;
#ifdef HAVE_OPENSSL
    /* The client closed the TCP connection without a close_notify. */
    if (ec == asio::ssl::error::stream_truncated)
        return 0;
#endif
    errno = errno_for(ec);
    return -1;
}

bool
transport_write(const struct iovec *vec, int count)
{
    Transport &t = tr();
    error_code ec;

#ifdef HAVE_OPENSSL
    if (t.tls) {
        if (!t.tls_ok) {
            errno = ECONNRESET;
            return false;
        }
        /* One TLS record per write rather than one per buffer: gather the
           buffers first, as SSL_writev did. */
        t.wbuf.clear();
        for (int i = 0; i < count; i++)
            t.wbuf.append(static_cast<const char *>(vec[i].iov_base),
                          vec[i].iov_len);
        ec = run(
            [&](auto handler) {
                asio::async_write(*t.tls, asio::buffer(t.wbuf), handler);
            },
            write_timeout());
        if (ec)
            tls_failed();
    } else
#endif
    {
        std::vector<asio::const_buffer> buffers;
        buffers.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; i++)
            buffers.emplace_back(vec[i].iov_base, vec[i].iov_len);
        ec = run(
            [&](auto handler) {
                asio::async_write(t.sock, buffers, handler);
            },
            write_timeout());
    }
    if (ec) {
        errno = errno_for(ec);
        return false;
    }
    return true;
}

bool
transport_write(const char *buf, size_t len)
{
    struct iovec vec;

    vec.iov_base = const_cast<char *>(buf);
    vec.iov_len = len;
    return transport_write(&vec, 1);
}

#ifdef HAVE_OPENSSL
SSL *
transport_start_tls(SSL_CTX *ctx, int timeout, void (*prepare)(SSL *))
{
    /* The context wraps tls.cc's SSL_CTX and is never destroyed: it lives
       as long as the process, and its destructor would free the SSL_CTX
       that tls.cc owns. */
    static auto *context = new asio::ssl::context(ctx);
    Transport &t = tr();

    t.tls.emplace(t.sock, *context);
    t.tls_ok = false;
    SSL *ssl = t.tls->native_handle();
    if (prepare != nullptr)
        prepare(ssl);

    const error_code ec = run(
        [&](auto handler) {
            t.tls->async_handshake(asio::ssl::stream_base::server,
                                   [handler](const error_code &e) mutable {
                                       handler(e, 0);
                                   });
        },
        timeout);
    if (ec) {
        if (ec == asio::error::timed_out)
            syslog(L_NOTICE, "TLS handshake timed out after %d seconds",
                   timeout);
        SSL_SESSION *session = SSL_get_session(ssl);
        if (session != nullptr)
            SSL_CTX_remove_session(ctx, session);
        t.tls.reset();
        return nullptr;
    }
    t.tls_ok = true;
    return ssl;
}
#endif

void
transport_close()
{
    if (!transport)
        return;
#ifdef HAVE_OPENSSL
    Transport &t = tr();
    if (t.tls && t.tls_ok) {
        /* Send close_notify and give the client a moment to answer. */
        run(
            [&](auto handler) {
                t.tls->async_shutdown([handler](const error_code &e) mutable {
                    handler(e, 0);
                });
            },
            2);
    }
    tls_conn = nullptr;
#endif
    transport.reset();
}
