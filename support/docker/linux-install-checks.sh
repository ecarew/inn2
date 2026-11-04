#!/bin/bash
#
#  Runs inside the container started by support/docker-install-checks.sh, as
#  the container's root, in a fresh copy of the source in the current
#  directory: a production build (no INN_RUNAS_*), installed with
#  cmake --install as root, then checked for what make install left:
#  ownership, modes, innbind the only setuid-root file, the first-install
#  data, a server that starts, and a reinstall that keeps the configuration.
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

[ "$fail" = 0 ] && echo "RESULT: PASS" || { echo "RESULT: FAIL"; tail -30 /tmp/rcnews.log; exit 1; }
