#  nnrpd Python authentication hook for support/live-innd.sh (python_auth:
#  "nnrpd_auth_test" in readers.conf, found in pathfilter;
#  doc/pod/hook-python.pod).  Accepts foo with password foopass, as INN's
#  test passwd file does, and refuses everything else.

from nnrpd import set_auth_hook


def value(attributes, key):
    item = attributes[key]
    return item.tobytes() if item is not None else b""


class Auth:
    def authen_init(self):
        pass

    def authenticate(self, attributes):
        if value(attributes, "user") == b"foo" and value(
            attributes, "pass"
        ) == b"foopass":
            return (281, "", "foo")
        return (481, "Bad user or password")

    def authen_close(self):
        pass


set_auth_hook(Auth())
