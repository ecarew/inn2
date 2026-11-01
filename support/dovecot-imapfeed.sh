#!/bin/bash
#
#  A throwaway dovecot for imapfeed's live checks (support/live-innd.sh):
#  IMAP on 127.0.0.1:143 and LMTP on 127.0.0.1:2003, the ports imapfeed is
#  fixed to, with one user "news" (password news-pw) whose mail is kept as
#  Maildir under DIR/mail/news; LMTP to news+GROUP saves into mailbox GROUP.
#
#  Usage: support/dovecot-imapfeed.sh start DIR [USER]
#         support/dovecot-imapfeed.sh stop DIR
#
#  live-innd.sh runs it as the current user, which works where that user may
#  bind port 143 (Linux with net.ipv4.ip_unprivileged_port_start=0).  Where
#  only root may (macOS), start it first with sudo, giving a directory and
#  the user live-innd.sh runs as:
#
#      sudo support/dovecot-imapfeed.sh start /tmp/inn-imap "$USER"
#      LIVE_IMAP_DIR=/tmp/inn-imap support/live-innd.sh build-live
#      sudo support/dovecot-imapfeed.sh stop /tmp/inn-imap
#
#  Only dovecot's master runs as root, to bind the ports; its other
#  processes and the mail belong to USER.  No INN program runs as root.

cmd=$1 dir=$2 user=${3:-$(id -un)}
[ -n "$dir" ] && { [ "$cmd" = start ] || [ "$cmd" = stop ]; } \
    || { sed -n '7,8p' "$0"; exit 2; }

if [ "$cmd" = stop ]; then
    base=$(cat "$dir/base_dir" 2>/dev/null) || exit 0
    pid=$(cat "$base/master.pid" 2>/dev/null)
    if [ -n "$pid" ] && kill "$pid" 2>/dev/null; then
        for _ in $(seq 1 20); do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.25
        done
    fi
    rm -rf "$base" "$dir/base_dir"
    exit 0
fi

dovecot=$(command -v dovecot || ls /opt/homebrew/sbin/dovecot \
          /usr/local/sbin/dovecot /usr/sbin/dovecot 2>/dev/null | head -1)
[ -n "$dovecot" ] || { echo "dovecot not found" >&2; exit 1; }
group=$(id -gn "$user") || exit 1

# Fresh mail each time, so nothing left from an earlier run passes a check.
# Socket paths are limited to about 104 bytes, so base_dir is short.
rm -rf "$dir/mail"
mkdir -p "$dir/mail" "$dir/state" || exit 1
dir=$(cd "$dir" && pwd -P)
base=$(mktemp -d /tmp/dv.XXXXXX) || exit 1
echo "$base" >"$dir/base_dir"
printf 'news:{PLAIN}news-pw\n' >"$dir/users"
chown -R "$user:$group" "$dir/mail"
cat >"$dir/dovecot.conf" <<EOF
dovecot_config_version = 2.4.0
dovecot_storage_version = 2.4.0
base_dir = $base
state_dir = $dir/state
log_path = $dir/dovecot.log
default_internal_user = $user
default_internal_group = $group
default_login_user = $user
protocols = imap lmtp
listen = 127.0.0.1
ssl = no
default_vsz_limit = 1T
auth_allow_cleartext = yes
auth_mechanisms = plain login
mail_driver = maildir
mail_path = $dir/mail/%{user}
mail_uid = $user
mail_gid = $group
first_valid_uid = 0
recipient_delimiter = +
lmtp_save_to_detail_mailbox = yes
lda_mailbox_autocreate = yes
passdb passwd-file {
  passwd_file_path = $dir/users
}
userdb static {
  fields {
    uid = $user
    gid = $group
    home = $dir/mail/%{user}
  }
}
service imap-login {
  chroot =
  inet_listener imap {
    port = 143
  }
  inet_listener imaps {
    port = 0
  }
}
service lmtp {
  inet_listener lmtp {
    port = 2003
  }
}
EOF
"$dovecot" -c "$dir/dovecot.conf" || exit 1
for _ in $(seq 1 20); do
    [ -s "$base/master.pid" ] && exit 0
    sleep 0.25
done
echo "dovecot did not start; see $dir/dovecot.log" >&2
exit 1
