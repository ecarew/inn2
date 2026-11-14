#!/usr/bin/env python3
"""NNTP-over-TLS client for support/live-innd.sh.

Usage: nntp-tls.py SCENARIO PORT [ARG]

Talks to nnrpd on 127.0.0.1:PORT and prints a transcript that the live
test checks.  Scenarios:

  implicit   TLS from the first byte (nnrpd -S): greeting, CAPABILITIES,
             AUTHINFO USER/PASS (foo/foopass), ARTICLE <live2@live.test>
  starttls   CAPABILITIES in plaintext ("plain: " lines), STARTTLS, then
             CAPABILITIES and DATE over TLS
  inject     "STARTTLS\\r\\nDATE\\r\\n" in one plaintext write; after the
             handshake, HELP.  The first reply over TLS must be HELP's:
             DATE was sent before TLS and must have been thrown away
  split      implicit TLS, one command sent as two TLS records with a
             pause between them
  compress   implicit TLS, then COMPRESS DEFLATE and DATE compressed
  idle       ARG is "tls" or "plain": greeting, MODE READER, then wait
             for nnrpd to close the idle connection; prints the seconds
  drop       implicit-TLS port: half a ClientHello, then close
  stall      implicit-TLS port: connect and send nothing; prints the
             seconds until nnrpd closes the connection (at most 60)
  starttls-stall
             plaintext port: STARTTLS, then no handshake; prints the
             seconds from the 382 reply until nnrpd closes (at most 60)

Certificates are not verified: the live test makes a throwaway one.
"""

import socket
import ssl
import sys
import time
import zlib

HOST = "127.0.0.1"


def context():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    return ctx


class Conn:
    """A line-oriented NNTP connection over a plain or TLS socket."""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def send(self, data):
        self.sock.sendall(data)

    def line(self):
        while b"\r\n" not in self.buf:
            data = self.sock.recv(65536)
            if not data:
                raise EOFError("connection closed")
            self.buf += data
        line, self.buf = self.buf.split(b"\r\n", 1)
        return line.decode("utf-8", "replace")

    def block(self):
        lines = []
        while True:
            line = self.line()
            if line == ".":
                return lines
            lines.append(line)

    def cmd(self, command, multi=False, prefix=""):
        self.send(command.encode() + b"\r\n")
        reply = self.line()
        print(prefix + reply)
        if multi and reply[:1] in ("1", "2"):
            for line in self.block():
                print(prefix + line)
        return reply

    def starttls(self):
        if self.buf:
            raise RuntimeError("data left in the plaintext buffer")
        self.sock = context().wrap_socket(self.sock)


def plain(port):
    return Conn(socket.create_connection((HOST, port), timeout=20))


def tls(port):
    sock = socket.create_connection((HOST, port), timeout=20)
    return Conn(context().wrap_socket(sock))


MULTI = ("CAPABILITIES", "ARTICLE", "HELP")


def multi(command):
    return command.split()[0] in MULTI


def implicit(port):
    c = tls(port)
    print(c.line())
    for command in ("CAPABILITIES", "AUTHINFO USER foo",
                    "AUTHINFO PASS foopass", "ARTICLE <live2@live.test>",
                    "QUIT"):
        c.cmd(command, multi(command))


def starttls(port):
    c = plain(port)
    print("plain: " + c.line())
    c.cmd("CAPABILITIES", True, "plain: ")
    if not c.cmd("STARTTLS", prefix="plain: ").startswith("382"):
        return
    c.starttls()
    print("tls: " + c.sock.version())
    for command in ("CAPABILITIES", "DATE", "QUIT"):
        c.cmd(command, multi(command))


def inject(port):
    c = plain(port)
    print("plain: " + c.line())
    c.send(b"STARTTLS\r\nDATE\r\n")
    reply = c.line()
    print("plain: " + reply)
    if not reply.startswith("382"):
        return
    # Anything the server sent after 382 in plaintext would be an error.
    if c.buf:
        print("plain: unexpected " + repr(c.buf))
        c.buf = b""
    c.starttls()
    c.send(b"HELP\r\n")
    print("first: " + c.line())


def split(port):
    c = tls(port)
    print(c.line())
    c.send(b"DA")
    time.sleep(0.5)
    c.send(b"TE\r\n")
    print(c.line())
    c.cmd("QUIT")


def compress(port):
    c = tls(port)
    print(c.line())
    if not c.cmd("COMPRESS DEFLATE").startswith("206"):
        return
    out = zlib.compressobj(wbits=-15)
    inp = zlib.decompressobj(wbits=-15)
    c.send(out.compress(b"DATE\r\nQUIT\r\n") + out.flush(zlib.Z_SYNC_FLUSH))
    text = b""
    while b"\r\n205" not in text and not text.startswith(b"205"):
        data = c.sock.recv(65536)
        if not data:
            break
        text += inp.decompress(data)
    for line in text.decode("utf-8", "replace").split("\r\n"):
        if line:
            print("inflated: " + line)


def idle(port, kind):
    c = tls(port) if kind == "tls" else plain(port)
    c.sock.settimeout(60)
    print(c.line())
    c.cmd("MODE READER")
    start = time.monotonic()
    try:
        while True:
            print("idle: " + c.line())
    except (EOFError, OSError):
        pass
    print("closed after %d s" % round(time.monotonic() - start))


def drop(port):
    sock = socket.create_connection((HOST, port), timeout=20)
    # A TLS handshake record header promising 512 bytes, and only 4 of them.
    sock.sendall(b"\x16\x03\x01\x02\x00\x01\x00\x01")
    sock.close()
    print("dropped")


def stall(port):
    sock = socket.create_connection((HOST, port), timeout=60)
    start = time.monotonic()
    try:
        while sock.recv(4096):
            pass
    except OSError:
        pass
    print("closed after %d s" % round(time.monotonic() - start))


def starttls_stall(port):
    c = plain(port)
    c.sock.settimeout(60)
    print("plain: " + c.line())
    if not c.cmd("STARTTLS", prefix="plain: ").startswith("382"):
        return
    start = time.monotonic()
    try:
        while c.sock.recv(4096):
            pass
    except OSError:
        pass
    print("closed after %d s" % round(time.monotonic() - start))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    scenario, port = sys.argv[1], int(sys.argv[2])
    args = sys.argv[3:]
    try:
        {
            "implicit": implicit, "starttls": starttls, "inject": inject,
            "split": split, "compress": compress, "idle": idle,
            "drop": drop, "stall": stall, "starttls-stall": starttls_stall,
        }[scenario](port, *args)
    except (OSError, EOFError, ssl.SSLError) as e:
        print("error: %s" % e)


if __name__ == "__main__":
    main()
