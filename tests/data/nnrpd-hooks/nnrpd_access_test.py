#  nnrpd Python access hook for support/live-innd.sh (python_access:
#  "nnrpd_access_test" in readers.conf; doc/pod/hook-python.pod).  Returns
#  readers.conf access parameters: reading and posting in test.* only.

from nnrpd import set_auth_hook


class Access:
    def access_init(self):
        pass

    def access(self, attributes):
        return {"read": "test.*", "post": "test.*"}

    def access_close(self):
        pass


set_auth_hook(Access())
