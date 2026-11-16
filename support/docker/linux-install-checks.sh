#!/bin/bash
#
#  Runs inside the container started by support/docker-install-checks.sh, as
#  the container's root, in a fresh copy of the source in the current
#  directory: a production build (no INN_RUNAS_*), installed with
#  cmake --install as root, then checked for what make install left:
#  ownership, modes, innbind the only setuid-root file, the first-install
#  data, the sample filters, a server that starts, and a reinstall that
#  keeps the configuration.
set -u
fail=0
res() { printf '  %-58s %s\n' "$1" "$2"; [ "$2" = PASS ] || fail=1; }
ok() { local l=$1; shift; if "$@" >/dev/null 2>&1; then res "$l" PASS; else res "$l" FAIL; fi; }

getent group news >/dev/null || groupadd -r news
getent passwd news >/dev/null || useradd -r -g news -d /usr/local/news -s /bin/sh news
cmake -S . -B build -G Ninja -DINN_TESTS=OFF >/tmp/cfg.log 2>&1 || { tail -20 /tmp/cfg.log; exit 1; }
cmake --build build -j"$(nproc)" >/tmp/build.log 2>&1 || { grep -m5 error /tmp/build.log; exit 1; }
if cmake --install build >/tmp/inst.log 2>&1; then
    res "cmake --install as root succeeds" PASS
else
    res "cmake --install as root succeeds" FAIL; tail -20 /tmp/inst.log; exit 1
fi

N=/usr/local/news
not_news=$(find $N ! -path $N/bin/innbind \( ! -user news -o ! -group news \) | head)
ok "everything but innbind owned by news:news" test -z "$not_news"
[ -n "$not_news" ] && echo "$not_news" | sed 's/^/    /'
ok "innbind is root:news 4550" test "$(stat -c '%U:%G %a' $N/bin/innbind)" = "root:news 4550"
setuid_root=$(find / -xdev -path /proc -prune -o -user root -perm -4000 -print 2>/dev/null | grep "^$N")
ok "innbind is the only setuid-root file" test "$setuid_root" = "$N/bin/innbind"
ok "no other file is setuid or setgid" test "$(find $N -perm /6000)" = "$N/bin/innbind"
ok "innd is news:news 550" test "$(stat -c '%U:%G %a' $N/bin/innd)" = "news:news 550"
ok "rnews is news:news 500" test "$(stat -c '%U:%G %a' $N/bin/rnews)" = "news:news 500"
ok "inews is news:news 550" test "$(stat -c '%U:%G %a' $N/bin/inews)" = "news:news 550"
ok "run/ is 750" test "$(stat -c '%a' $N/run)" = 750
ok "history database created (makedbz -i -o)" test -s $N/db/history.dir -a -e $N/db/history
ok "history files owned by news" test -z "$(find $N/db ! -user news)"
# The sample filters and hooks, as site/Makefile installs them, whether or
# not the build embeds Perl or Python; the .pl.in ones with their #! line.
filters="startup_innd.pl filter_innd.pl filter_nnrpd.pl filter_innd.py INN.py
nnrpd.py nnrpd_auth.pl nnrpd_access.pl nnrpd_auth.py nnrpd_access.py
nnrpd_dynamic.py"
bad_filters=$(for f in $filters; do
    [ "$(stat -c '%U:%G %a' $N/bin/filter/$f 2>/dev/null)" = "news:news 644" ] || echo "$f"
done)
ok "sample filters installed in bin/filter, news:news 644" test -z "$bad_filters"
[ -n "$bad_filters" ] && echo "$bad_filters" | sed 's/^/    /'
ok "nnrpd_auth.pl has its #! line filled in" sh -c "head -1 $N/bin/filter/nnrpd_auth.pl | grep -q '^#! */.*perl'"

# Man pages and documents (doc/CMakeLists.txt), built from POD.
M=$N/share/man
ok "man pages installed (innd.8, inn.conf.5, inews.1, libinn.3)" test -s $M/man8/innd.8 -a -s $M/man5/inn.conf.5 -a -s $M/man1/inews.1 -a -s $M/man3/libinn.3
ok "embedded-POD pages installed (pgpverify.1, mailpost.8, 3pm)" test -s $M/man1/pgpverify.1 -a -s $M/man8/mailpost.8 -a -s "$M/man3/INN::Config.3pm"
ok "at least 120 man pages" test "$(find $M -type f | wc -l)" -ge 120
ok "man links resolve (hisv6.5, localgroups.5, imapfeed.8)" test -s $M/man5/hisv6.5 -a -s $M/man5/localgroups.5 -a -s $M/man8/imapfeed.8
ok "documents installed (INSTALL, NEWS, HACKING, hook-perl)" test -s $N/doc/INSTALL -a -s $N/doc/NEWS -a -s $N/doc/HACKING -a -s $N/doc/hook-perl

# The container's host name has no domain; an administrator sets one.
sed -i 's/^#* *domain:.*/domain: example.test/' $N/etc/inn.conf
grep -q '^domain: example.test' $N/etc/inn.conf || echo 'domain: example.test' >>$N/etc/inn.conf
# Start the server the way an administrator does: rc.news as news.  innd
# binds port 119 through the setuid innbind.
su news -s /bin/sh -c "$N/bin/rc.news" >/tmp/rcnews.log 2>&1
for _ in $(seq 50); do $N/bin/ctlinnd -s mode >/dev/null 2>&1 && break; sleep 0.2; done
ok "rc.news starts innd as news" $N/bin/ctlinnd -s mode
ok "innd runs as news" test "$(ps -o user= -C innd | head -1)" = news
ok "innd listens on port 119 (through innbind)" bash -c "exec 3<>/dev/tcp/127.0.0.1/119 && head -1 <&3 | grep -q '^20[01] '"
su news -s /bin/sh -c "$N/bin/ctlinnd -s shutdown test" >/dev/null 2>&1
sleep 1

# A second install keeps the configuration and data.
echo "# local" >>$N/etc/inn.conf
cmake --install build >/tmp/inst2.log 2>&1
ok "reinstall keeps an edited inn.conf" sh -c "tail -1 $N/etc/inn.conf | grep -q '# local'"
ok "reinstall keeps innbind setuid root" test "$(stat -c '%U:%G %a' $N/bin/innbind)" = "root:news 4550"

# INN_SETGID_INEWS, in another prefix.
cmake -S . -B build-sg -G Ninja -DINN_TESTS=OFF -DINN_SETGID_INEWS=ON -DINN_PREFIX=/opt/news-sg >/dev/null 2>&1
cmake --build build-sg -j"$(nproc)" >/dev/null 2>&1
cmake --install build-sg >/tmp/inst3.log 2>&1
ok "INN_SETGID_INEWS: inews is news:news 2555" test "$(stat -c '%U:%G %a' /opt/news-sg/bin/inews)" = "news:news 2555"

# The directory options, in the FHS layout a package uses.
cmake -S . -B build-fhs -G Ninja -DINN_TESTS=OFF -DINN_PREFIX=/usr/lib/news \
    -DINN_PATH_CONFIG=/etc/news -DINN_DB_DIR=/var/lib/news \
    -DINN_SPOOL_DIR=/var/spool/news -DINN_LOG_DIR=/var/log/news \
    -DINN_RUN_DIR=/run/news -DINN_MAN_DIR=/usr/share/man \
    -DINN_DOC_DIR=/usr/share/doc/inn \
    -DINN_SYSTEMD_UNIT_DIR=/usr/lib/systemd/system >/dev/null 2>&1
cmake --build build-fhs -j"$(nproc)" >/dev/null 2>&1
ok "FHS layout: cmake --install succeeds" cmake --install build-fhs
ok "FHS layout: inn.conf points at the directories" sh -c "grep -q '^pathdb: *[/]var/lib/news\$' /etc/news/inn.conf && grep -q '^pathspool: *[/]var/spool/news\$' /etc/news/inn.conf && grep -q '^pathlog: *[/]var/log/news\$' /etc/news/inn.conf && grep -q '^pathrun: *[/]run/news\$' /etc/news/inn.conf"
ok "FHS layout: directories and data owned by news" test -z "$(find /var/lib/news /var/spool/news /var/log/news /run/news /etc/news ! -user news)"
ok "FHS layout: history and active created" test -s /var/lib/news/active -a -e /var/lib/news/history
ok "FHS layout: man pages in /usr/share/man" test -s /usr/share/man/man8/innd.8
ok "FHS layout: man pages outside INN's directories stay root's" test -z "$(find /usr/share/man /usr/share/doc/inn ! -user root)"
U=/usr/lib/systemd/system/inn.service
ok "FHS layout: inn.service installed root:root 644" test "$(stat -c '%U:%G %a' $U)" = "root:root 644"
# No systemd in the image: the unit is only a file here, Type=forking
# without libsystemd; the Lima VM tests it under systemd.
ok "FHS layout: inn.service is Type=forking, runs as news" sh -c "grep -qx 'Type=forking' $U && grep -qx 'User=news' $U && grep -qx 'RuntimeDirectory=news' $U && grep -qx 'ProtectHome=true' $U"
ok "cert target: key and certificate made" sh -c "printf '\\n\\n\\n\\n\\nnews.example.test\\n\\n' | cmake --build build-fhs --target cert >/dev/null 2>&1"
ok "cert target: cert.pem news:news 640, key.pem news:news 600" test "$(stat -c '%U:%G %a' /etc/news/cert.pem /etc/news/key.pem | tr '\n' ' ')" = "news:news 640 news:news 600 "

# INN_NEWS_USER and INN_NEWS_GROUP: another account, in another prefix.
getent group usenet >/dev/null || groupadd -r usenet
getent passwd usenet >/dev/null || useradd -r -g usenet -d /opt/news-usenet -s /bin/sh usenet
U=/opt/news-usenet
cmake -S . -B build-usenet -G Ninja -DINN_TESTS=OFF -DINN_PREFIX=$U \
    -DINN_NEWS_USER=usenet -DINN_NEWS_GROUP=usenet >/dev/null 2>&1
cmake --build build-usenet -j"$(nproc)" >/dev/null 2>&1
ok "INN_NEWS_USER=usenet: cmake --install succeeds" cmake --install build-usenet
ok "INN_NEWS_USER=usenet: everything but innbind owned by usenet" test -z "$(find $U ! -path $U/bin/innbind \( ! -user usenet -o ! -group usenet \))"
ok "INN_NEWS_USER=usenet: innbind is root:usenet 4550" test "$(stat -c '%U:%G %a' $U/bin/innbind)" = "root:usenet 4550"
ok "INN_NEWS_USER=usenet: innconfval runasuser is usenet" test "$($U/bin/innconfval runasuser)" = usenet
ok "INN_NEWS_USER=root is refused" sh -c "! cmake -S . -B build-root-user -DINN_TESTS=OFF -DINN_NEWS_USER=root >/dev/null 2>&1"

[ "$fail" = 0 ] && echo "RESULT: PASS" || { echo "RESULT: FAIL"; tail -30 /tmp/rcnews.log; exit 1; }
