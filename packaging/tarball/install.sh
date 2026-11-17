#!/bin/sh
#
#  Install INN++ from the generic Linux tarball into /usr/local/news.
#  Run as root from the unpacked tarball:
#
#      sudo ./install.sh
#
#  Creates the news group and user when absent, copies the tree, gives
#  INN's directories to news and makes innbind setuid root (root:news,
#  4550), the only setuid-root file.  Configuration files and site data
#  (inn.conf, active, newsgroups, history) are created only when absent,
#  so running it again upgrades the programs and keeps the site's files.
#  On a systemd host the unit goes to /etc/systemd/system, not enabled.
set -eu

prefix=/usr/local/news
user=news
group=news
here=$(cd "$(dirname "$0")" && pwd)
src=$here/root$prefix
defaults=$src/share/inn2-cxx

if [ "$(id -u)" != 0 ]; then
    echo "install.sh: run as root (sudo ./install.sh)" >&2
    exit 1
fi
if [ ! -x "$src/bin/innd" ]; then
    echo "install.sh: $src/bin/innd not found; run from the unpacked tarball" >&2
    exit 1
fi

# The account INN runs as.  Debian and others have news already (uid 9).
nologin=/usr/sbin/nologin
[ -x "$nologin" ] || nologin=/sbin/nologin
[ -x "$nologin" ] || nologin=/bin/false
if ! getent group "$group" >/dev/null; then
    groupadd -r "$group"
    echo "Created group $group"
fi
if ! getent passwd "$user" >/dev/null; then
    useradd -r -g "$group" -d "$prefix" -s "$nologin" -c "InterNetNews" "$user"
    echo "Created user $user"
fi

# as_news CMD...: run a command as news, in the directory it is in.
as_news() {
    if command -v runuser >/dev/null 2>&1; then
        runuser -u "$user" -- "$@"
    else
        su -s /bin/sh "$user" -c '"$0" "$@"' -- "$@"
    fi
}

# The programs, libraries, documents and default configuration: replaced
# on every run.  etc/ is staged under share/inn2-cxx/etc and copied below
# only where the site has no file yet.
mkdir -p "$prefix"
(cd "$here/root$prefix" && tar -cf - .) | (cd "$prefix" && tar -xpf -)

mkdir -p "$prefix/etc"
created=false
(cd "$defaults/etc" && find . -type f) | while read -r f; do
    if [ ! -e "$prefix/etc/$f" ]; then
        mkdir -p "$(dirname "$prefix/etc/$f")"
        cp -p "$defaults/etc/$f" "$prefix/etc/$f"
    fi
done
if [ ! -e "$prefix/etc/inn.conf" ]; then
    host=$(hostname -f 2>/dev/null || hostname)
    sed "s/@PATHHOST@/$host/" "$defaults/inn.conf" >"$prefix/etc/inn.conf"
    chmod 644 "$prefix/etc/inn.conf"
    created=true
fi

# Ownership, as cmake --install does as root: INN's tree is news's, the
# unit stays root's, innbind is root:news 4550.  chown clears setuid, so
# the mode is set after it.
chown -R "$user:$group" "$prefix"
chown -R root:root "$prefix/share/systemd"
chown root:"$group" "$prefix/bin/innbind"
chmod 4550 "$prefix/bin/innbind"

# First-install site data.
db=$prefix/db
for f in active newsgroups; do
    if [ ! -e "$db/$f" ]; then
        install -o "$user" -g "$group" -m 664 "$defaults/$f.minimal" "$db/$f"
        created=true
    fi
done
[ -e "$db/active.times" ] \
    || install -o "$user" -g "$group" -m 664 /dev/null "$db/active.times"
hismethod=$(as_news "$prefix/bin/innconfval" hismethod)
histdir=$(as_news "$prefix/bin/innconfval" pathhistory)
if [ "$hismethod" = hissqlite ]; then
    [ -e "$histdir/history.sqlite" ] || as_news "$prefix/bin/makehistory"
elif [ ! -e "$histdir/history" ]; then
    install -o "$user" -g "$group" -m 664 /dev/null "$histdir/history"
    (cd "$histdir" && as_news "$prefix/bin/makedbz" -i -o)
fi

# systemd: install the unit, but leave enabling it to the site.
unit=
if [ -d /run/systemd/system ]; then
    install -o root -g root -m 644 "$prefix/share/systemd/inn.service" \
        /etc/systemd/system/inn.service
    systemctl daemon-reload
    unit=yes
fi

echo "INN++ installed in $prefix."
if $created; then
    echo "A minimal active file and history database were created."
    echo "Configure INN in $prefix/etc (see $prefix/doc and inn.conf(5))."
fi
if [ -n "$unit" ]; then
    echo "Start it with: systemctl enable --now inn"
else
    echo "Start it with: $prefix/bin/rc.news (as $user)"
fi
echo "Add $prefix/share/man to MANPATH for the man pages."
