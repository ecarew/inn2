buffchan: the buffered file exploder {#buffchan}
====================================

buffchan (`backends/buffchan.cc`) is one process that innd feeds as an
exploder channel (`Tx` in newsfeeds). Each line innd sends carries the
article data (by default one field, usually a token) followed by the names
of the member sites (`Tm`) that should get it. buffchan appends that data to
one file per site, normally in `pathoutgoing`, where batch scripts pick it
up. A single process takes the place of many separate file feeds.

Data flow
---------

\dot
digraph buffchan_flow {
    rankdir=TB;
    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10];
    edge [fontname="Helvetica", fontsize=9];

    newsfeeds [label="newsfeeds\n(Tx exploder, Tm sites)"];
    ctlinnd   [label="ctlinnd\n(flush, drop, send)"];
    innd      [label="innd\n(channel feed writer)"];
    map       [label="map file (-m)\nsite to file name", shape=note];
    buffchan  [label="buffchan\n(one process, many files)", style="rounded,bold"];
    errlog    [label="errlog\n(stderr unless -r)", shape=note];

    subgraph cluster_out {
        label="pathoutgoing/ (or -d dir, -s format)"; labelloc=b;
        fontname="Helvetica"; fontsize=10; style=dashed;
        siteA [label="siteA"];
        siteB [label="siteB"];
        siteN [label="siteN ..."];
    }

    batch [label="batch scripts\n(send-uucp, nntpsend ...)"];
    peers [label="peers", shape=ellipse];

    { rank=same; newsfeeds; innd; ctlinnd; }
    { rank=same; map; buffchan; errlog; }

    newsfeeds -> innd;
    ctlinnd -> innd;
    innd -> buffchan [label="pipe: \"data site site ...\"\nand \"!command\" lines"];
    map -> buffchan;
    buffchan -> errlog;
    buffchan -> siteA;
    buffchan -> siteB;
    buffchan -> siteN;
    siteA -> batch;
    siteB -> batch;
    siteN -> batch;
    batch -> peers;
}
\enddot

Interactions
------------

How the parts talk over time, from left to right. A line that starts with
`!` is a command; a data line that begins with `!` is sent as `!!`.

\dot
digraph buffchan_sequence {
    rankdir=LR;
    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10];
    edge [fontname="Helvetica", fontsize=9];
    newrank=true;

    subgraph cluster_file { label="site files"; fontname="Helvetica"; style=dashed;
        f0 [label="open if needed,\nappend"]; fl [label="fflush, or\nclose + reopen"];
        f1 [label="close + reopen"]; f2 [label="close,\nmark dropped"];
        f4 [label="close all, exit"]; }

    subgraph cluster_bc   { label="buffchan";   fontname="Helvetica"; style=dashed;
        b0 [label="split \"tok siteA siteB\"\nby site"]; bl [label="per-site limits\n(-l -L -c -C)"];
        b1 [label="!flush siteA"]; b2 [label="!drop siteA"];
        b3 [label="!readmap:\nre-read map file"]; b4 [label="EOF / SIGTERM ...\n(a second signal\nkills at once)"]; }
    subgraph cluster_innd { label="innd";       fontname="Helvetica"; style=dashed;
        i0 [label="article accepted"]; i1 [label="forward"]; i2 [label="forward"];
        i3 [label="forward"]; i4 [label="closes pipe,\nor a signal"]; }
    subgraph cluster_ctl  { label="ctlinnd";    fontname="Helvetica"; style=dashed;
        c1 [label="flush siteA"]; c2 [label="drop siteA"];
        c3 [label="send exploder readmap"]; }
    { rank=same; i0; b0; f0; }
    { rank=same; bl; fl; }
    { rank=same; c1; i1; b1; f1; }
    { rank=same; c2; i2; b2; f2; }
    { rank=same; c3; i3; b3; }
    { rank=same; i4; b4; f4; }

    i0 -> b0;
    b0 -> f0;
    bl -> fl;
    c1 -> i1 -> b1 -> f1;
    c2 -> i2 -> b2 -> f2;
    c3 -> i3 -> b3;
    i4 -> b4 -> f4;

    /* Keep the events in order down each column. */
    edge [style=invis];
    i0 -> i1 -> i2 -> i3 -> i4;
    b0 -> bl -> b1 -> b2 -> b3 -> b4;
    f0 -> fl -> f1 -> f2 -> f4;
    c1 -> c2 -> c3;
}
\enddot

Behaviour worth knowing
-----------------------

- `drop` closes the site's file and marks it dropped, but the next article
  for that site reopens the file and writes to it; the mark only stops a
  flush from reopening it.
- innd adds the site name to `!flush` and `!drop` only when the member's
  newsfeeds entry asks for names; a bare `!flush` or `!drop` acts on every
  site.
- Each site has its own timer for the time limits (`-L`, `-C`), so an idle
  site's data is written out on time, not held until its next article.
  (Before the move to Boost.Asio the limits were checked only when a line
  arrived for the site.) `SIGALRM` is just another interrupt.
- One Boost.Asio `io_context` runs the program: the read of standard
  input, the site timers, and the signals. innd's channel, a pipe, is read
  asynchronously. Input the event loop cannot wait on (a regular file, or
  on macOS a named FIFO or a terminal) is read by a thread that hands each
  chunk to the loop and waits for it to be handled.
- Site names are case-sensitive.
- `!begin` (sent when a member site is added) is accepted and does nothing.
  A line too long for the input buffer is skipped with a warning.
