#!/bin/bash
# linux-vm-checks.sh provision|before-reboot|after-reboot: run as root in
# the Linux VM by support/vm-linux-install-checks.sh.
#   provision      the toolchain support/docker/Dockerfile.linux installs:
#                  Debian packages, CMake 4.3 from PyPI, Boost 1.92 headers
#                  (the same release, checked against the same SHA-256)
#   before-reboot  after linux-install-checks.sh: start innd from a systemd
#                  unit, run news.daily as news, enable the unit
#   after-reboot   innd is running again, as news, serving port 119
set -u
N=/usr/local/news
fail=0
res() { printf '  %-58s %s\n' "$1" "$2"; [ "$2" = PASS ] || fail=1; }
ok() { local l=$1; shift; if "$@" >/dev/null 2>&1; then res "$l" PASS; else res "$l" FAIL; fi; }
port119() { bash -c "exec 3<>/dev/tcp/127.0.0.1/119 && head -1 <&3 | grep -q '^20[01] '"; }
wait_innd() { for _ in $(seq 50); do $N/bin/ctlinnd -s mode >/dev/null 2>&1 && return 0; sleep 0.2; done; return 1; }

case ${1:-} in
provision)
    set -e
    export DEBIAN_FRONTEND=noninteractive
    apt-get -qq update
    apt-get -qq install -y --no-install-recommends build-essential \
        ninja-build perl python3 python3-venv flex bison libsqlite3-dev \
        zlib1g-dev ca-certificates procps curl bzip2 cron >/dev/null
    python3 -m venv /opt/cmake
    /opt/cmake/bin/pip install -q 'cmake>=4.3'
    ln -sf /opt/cmake/bin/cmake /opt/cmake/bin/ctest /usr/local/bin/
    sha=$(sed -n 's/^ARG BOOST_SHA256=//p' /src/support/docker/Dockerfile.linux)
    cd /tmp
    curl -sfLO https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.bz2
    echo "$sha  boost_1_92_0.tar.bz2" | sha256sum -c --quiet
    tar xjf boost_1_92_0.tar.bz2
    (cd boost_1_92_0 && ./bootstrap.sh >/dev/null \
        && ./b2 --prefix=/usr/local --with-headers install >/dev/null)
    rm -rf boost_1_92_0 boost_1_92_0.tar.bz2
    exit 0
    ;;
before-reboot)
    # linux-install-checks.sh stopped its innd; start it the systemd way.
    # contrib/sample.init.systemd adapted: Type=forking (no sd_notify until
    # systemd support is an option), the /usr/local/news paths, no
    # AmbientCapabilities (innd binds port 119 through innbind), and
    # ReadWritePaths for the prefix, which ProtectSystem=full makes
    # read-only with the rest of /usr.
    cat >/etc/systemd/system/innd.service <<EOF
[Unit]
Description=InterNetNews News Server (INN++)
After=network-online.target
Wants=network-online.target
ConditionPathExists=$N/etc/inn.conf

[Service]
Type=forking
ExecStart=$N/bin/rc.news
ExecStop=$N/bin/rc.news stop
PIDFile=$N/run/innd.pid
User=news
Group=news
PrivateTmp=true
ProtectControlGroups=true
ProtectHome=true
ProtectSystem=full
ReadWritePaths=$N
LimitNOFILE=infinity

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
    systemctl start innd.service
    wait_innd
    ok "systemd starts innd (innd.service active)" systemctl is-active --quiet innd.service
    ok "innd under systemd runs as news" test "$(ps -o user= -C innd | head -1)" = news
    ok "innd under systemd serves port 119" port119
    # news.daily as news, the way cron runs it (waiting for cron's schedule
    # would add hours; the crontab line is checked for syntax only).
    ok "news.daily as news succeeds" su news -s /bin/sh -c "$N/bin/news.daily delayrm"
    ok "innd still running after news.daily" $N/bin/ctlinnd -s mode
    echo "15 4 * * * $N/bin/news.daily expireover delayrm" | crontab -u news -
    ok "news crontab installed" sh -c "crontab -u news -l | grep -q news.daily"
    systemctl enable --quiet innd.service
    ok "innd.service enabled" systemctl is-enabled --quiet innd.service
    ;;
after-reboot)
    wait_innd
    ok "after reboot: innd.service active" systemctl is-active --quiet innd.service
    ok "after reboot: innd runs as news" test "$(ps -o user= -C innd | head -1)" = news
    ok "after reboot: port 119 served" port119
    ok "after reboot: innbind still root:news 4550" test "$(stat -c '%U:%G %a' $N/bin/innbind)" = "root:news 4550"
    systemctl stop innd.service
    ok "systemctl stop stops innd" sh -c "! pgrep -x innd"
    ;;
*) echo "usage: $0 provision|before-reboot|after-reboot" >&2; exit 2 ;;
esac
[ "$fail" = 0 ] || { journalctl -u innd.service --no-pager | tail -20; exit 1; }
