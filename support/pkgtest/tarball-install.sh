#!/bin/sh
#
#  Install check for the generic Linux tarball, run as root in a clean
#  container with the tarball in /pkg (support/pkgtest/run.sh).  Installs
#  the run-time libraries the README names, runs install.sh, checks owners
#  and modes, and reruns it over a site's edits.  Prints one line per
#  check and a RESULT line.
fail=0
ok() { d=$1; shift; if "$@"; then echo "ok   $d"; else echo "FAIL $d"; fail=1; fi; }
P=/usr/local/news
if command -v apt-get >/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq libsqlite3-0 libzstd1 libpam0g libcrypt1 >/dev/null 2>&1
elif command -v dnf >/dev/null; then
    dnf install -y -q sqlite-libs libzstd pam libxcrypt shadow-utils \
        hostname util-linux >/dev/null 2>&1
fi
cd /tmp && tar -xzf /pkg/inn2-cxx-*-linux-*.tar.gz && cd inn2-cxx-*/ || exit 1
ok "non-root refused" sh -c "! setpriv --reuid=65534 --regid=65534 --clear-groups ./install.sh >/dev/null 2>&1"
ok "install.sh runs" sh -c "./install.sh > /tmp/inst.log 2>&1"
tail -4 /tmp/inst.log
ok "news user exists" getent passwd news
ok "inn.conf made, pathhost this host" sh -c "grep -q \"^pathhost: *$(hostname -f 2>/dev/null || hostname)\$\" $P/etc/inn.conf"
ok "innbind root:news 4550" test "$(stat -c '%U:%G %a' $P/bin/innbind)" = "root:news 4550"
ok "innbind the only setuid-root file" test "$(find $P -perm -4000 | tr '\n' ' ')" = "$P/bin/innbind "
ok "tree owned by news (but the unit)" test -z "$(find $P ! -user news ! -path "$P/share/systemd*" ! -path $P/bin/innbind)"
ok "unit root-owned" test "$(stat -c %U $P/share/systemd/inn.service)" = root
ok "active, newsgroups created" test -s $P/db/active -a -s $P/db/newsgroups
ok "history database initialized" test -e $P/db/history.dir
ok "innconfval runs as news" runuser -u news -- $P/bin/innconfval pathnews
ok "innd -h runs (libraries resolve)" sh -c "$P/bin/innd -h 2>&1 | grep -qi usage"
ok "no program has a missing library" sh -c "! for f in $P/bin/*; do [ -f \$f ] && ldd \$f 2>/dev/null; done | grep -q 'not found'"
echo "# site edit" >> $P/etc/inn.conf
echo "local.test 0000000000 0000000001 y" >> $P/db/active
ok "rerun (upgrade) runs" sh -c "./install.sh > /tmp/inst2.log 2>&1"
ok "rerun keeps inn.conf" grep -q "^# site edit" $P/etc/inn.conf
ok "rerun keeps active" grep -q "^local.test " $P/db/active
ok "innbind still 4550 after rerun" test "$(stat -c '%a' $P/bin/innbind)" = 4550
[ "$fail" = 0 ] && echo "RESULT: PASS" || { echo "RESULT: FAIL"; exit 1; }
