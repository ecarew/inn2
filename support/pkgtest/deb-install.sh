#!/bin/sh
#
#  Install check for the inn2-cxx .deb, run as root in a clean Debian or
#  Ubuntu container with the .deb in /pkg (support/pkgtest/run.sh).
#  Installs it, checks owners and modes, reinstalls over a site's edits,
#  and purges.  Prints one line per check and a RESULT line.
fail=0
ok() { d=$1; shift; if "$@"; then echo "ok   $d"; else echo "FAIL $d"; fail=1; fi; }
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
ok "apt installs the .deb" sh -c "apt-get install -y -qq /pkg/inn2-cxx_*.deb > /tmp/apt.log 2>&1"
tail -5 /tmp/apt.log
ok "inn.conf made by postinst" test -s /etc/news/inn.conf
ok "pathhost is this host" sh -c "grep -q \"^pathhost: *$(hostname -f 2>/dev/null || hostname)\$\" /etc/news/inn.conf"
ok "inn.conf news:news 644" test "$(stat -c '%U:%G %a' /etc/news/inn.conf)" = "news:news 644"
ok "innbind root:news 4550" test "$(stat -c '%U:%G %a' /usr/lib/news/bin/innbind)" = "root:news 4550"
ok "innbind the only setuid-root file" test "$(find /usr/lib/news /var/lib/news /var/spool/news /etc/news -perm -4000 | tr '\n' ' ')" = "/usr/lib/news/bin/innbind "
ok "active, newsgroups, history created" test -s /var/lib/news/active -a -s /var/lib/news/newsgroups -a -e /var/lib/news/history
ok "history database initialized" test -e /var/lib/news/history.dir
ok "data owned by news" test -z "$(find /var/lib/news /var/spool/news /var/log/news ! -user news)"
ok "unit root:root 644" test "$(stat -c '%U:%G %a' /usr/lib/systemd/system/inn.service)" = "root:root 644"
ok "man pages root-owned" test -z "$(find /usr/share/man/man8 -name 'inn*' ! -user root)"
ok "innconfval reads inn.conf" runuser -u news -- /usr/lib/news/bin/innconfval pathnews
# Upgrade path: an edited inn.conf and active survive a reinstall.
echo "# site edit" >> /etc/news/inn.conf
echo "local.test 0000000000 0000000001 y" >> /var/lib/news/active
apt-get install -y -qq --reinstall /pkg/inn2-cxx_*.deb > /tmp/apt2.log 2>&1
ok "reinstall keeps the site's inn.conf" grep -q "^# site edit" /etc/news/inn.conf
ok "reinstall keeps the site's active" grep -q "^local.test " /var/lib/news/active
apt-get purge -y -qq inn2-cxx > /tmp/apt3.log 2>&1
ok "purge removes inn.conf" test ! -e /etc/news/inn.conf
ok "purge keeps the site's active" test -e /var/lib/news/active
[ "$fail" = 0 ] && echo "RESULT: PASS" || { echo "RESULT: FAIL"; exit 1; }
