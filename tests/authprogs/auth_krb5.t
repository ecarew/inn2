#! /bin/sh
#
# Test suite for auth_krb5, against a throwaway Kerberos realm: a KDC run as
# the current user on a high port, in a temporary directory, with its own
# krb5.conf, database and keytab.
#
# Needs AUTH_KRB5 (the program) and INN_KRB5_CONFIG (the krb5-config it was
# built with; the KDC tools are looked for beside it, in ../sbin); skips if
# either is missing, as when the build found a Kerberos without a KDC.
# KRB5_TEST_PORT overrides the port (default 18888).

count=1
printcount() {
    echo "$1 $count $2"
    count=$(expr $count + 1)
}

skip_all() {
    echo "1..0 # skip $1"
    exit 0
}

if [ -z "$AUTH_KRB5" ] || [ ! -x "$AUTH_KRB5" ]; then
    skip_all "auth_krb5 not built"
fi
if [ -z "$INN_KRB5_CONFIG" ]; then
    skip_all "INN_KRB5_CONFIG not set"
fi
sbin=$(cd "$(dirname "$INN_KRB5_CONFIG")/../sbin" 2>/dev/null && pwd)
for p in krb5kdc kdb5_util kadmin.local; do
    if [ -z "$sbin" ] || [ ! -x "$sbin/$p" ]; then
        skip_all "no $p beside $INN_KRB5_CONFIG"
    fi
done

port=${KRB5_TEST_PORT:-18888}
dir=$(mktemp -d "${TMPDIR:-/tmp}/auth_krb5.XXXXXX") || exit 1
dir=$(cd "$dir" && pwd -P)
kdc_pid=
cleanup() {
    if [ -n "$kdc_pid" ]; then
        kill "$kdc_pid" 2>/dev/null
        wait "$kdc_pid" 2>/dev/null
    fi
    rm -rf "$dir"
}
trap cleanup EXIT

# krb5conf FILE [LIBDEFAULTS-LINE]: a krb5.conf for the test realm.
krb5conf() {
    cat >"$1" <<EOF
[libdefaults]
    default_realm = TEST.INN
    dns_lookup_kdc = false
    dns_lookup_realm = false
    rdns = false
    $2
[realms]
    TEST.INN = {
        kdc = 127.0.0.1:$port
    }
EOF
}
krb5conf "$dir/krb5.conf"
krb5conf "$dir/krb5-nofail.conf" "verify_ap_req_nofail = true"
cat >"$dir/kdc.conf" <<EOF
[kdcdefaults]
    kdc_listen = 127.0.0.1:$port
    kdc_tcp_listen = 127.0.0.1:$port
[realms]
    TEST.INN = {
        database_name = $dir/principal
        key_stash_file = $dir/stash
        acl_file = $dir/kadm5.acl
    }
[logging]
    kdc = FILE:$dir/kdc.log
EOF
KRB5_CONFIG=$dir/krb5.conf
KRB5_KDC_PROFILE=$dir/kdc.conf
KRB5_KTNAME=$dir/keytab
export KRB5_CONFIG KRB5_KDC_PROFILE KRB5_KTNAME

# The realm: tester and tester/admin with passwords, and a host key for
# auth_krb5 to verify the tickets it gets against.
kadmin() { "$sbin/kadmin.local" -q "$1" >>"$dir/setup.log" 2>&1; }
"$sbin/kdb5_util" create -s -P master-pw -r TEST.INN >"$dir/setup.log" 2>&1 \
    || { cat "$dir/setup.log"; exit 1; }
kadmin "addprinc -pw tester-pw tester"
kadmin "addprinc -pw admin-pw tester/admin"
kadmin "addprinc -randkey host/localhost"
kadmin "ktadd -k $dir/keytab host/localhost"

"$sbin/krb5kdc" -n >"$dir/kdc.out" 2>&1 &
kdc_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    grep -q 'commencing operation' "$dir/kdc.log" 2>/dev/null && break
    sleep 0.25
done
grep -q 'commencing operation' "$dir/kdc.log" 2>/dev/null \
    || { cat "$dir/kdc.out" "$dir/kdc.log" 2>/dev/null; exit 1; }

# run USER PASSWORD [ARGS]: run auth_krb5 as nnrpd does, the username and
# password on stdin; output in $dir/out, status in $status.
run() {
    user=$1 password=$2
    shift 2
    printf 'ClientAuthname: %s\r\nClientPassword: %s\r\n.\r\n' \
        "$user" "$password" | "$AUTH_KRB5" "$@" >"$dir/out" 2>&1
    status=$?
}

# accepted DESCRIPTION USER: status 0 and the User: line nnrpd reads.
accepted() {
    if [ $status = 0 ] && [ "$(cat "$dir/out")" = "$(printf 'User:%s\r' "$2")" ]
    then
        printcount ok "- $1"
    else
        printcount "not ok" "- $1"
        sed 's/^/#   /' "$dir/out"
    fi
}

# refused DESCRIPTION MESSAGE: a failure status and MESSAGE on stderr.
refused() {
    if [ $status != 0 ] && grep -q "$2" "$dir/out"; then
        printcount ok "- $1"
    else
        printcount "not ok" "- $1"
        sed 's/^/#   /' "$dir/out"
    fi
}

echo 1..8

run tester tester-pw
accepted "the right password" tester
run tester wrong-pw
refused "a wrong password" "bad password for tester"
run nobody nobody-pw
refused "an unknown user" "unknown user nobody"
run "tester@OTHER.REALM" tester-pw
refused "a user in another realm" "username contains @"
run tester admin-pw -i admin
accepted "-i adds the instance" tester/admin
run tester tester-pw -i admin
refused "-i with the user's own password" "bad password for tester/admin"

# The ticket is checked against the host key in the keytab, so a KDC that
# does not know the key (a forged one) is not believed.  Without a keytab
# MIT Kerberos skips that check unless verify_ap_req_nofail is set.
KRB5_KTNAME=$dir/no-such-keytab
run tester tester-pw
accepted "no keytab: accepted unless verify_ap_req_nofail is set" tester
KRB5_CONFIG=$dir/krb5-nofail.conf
run tester tester-pw
refused "no keytab, verify_ap_req_nofail: refused" "Kerberos authentication for tester failed"
