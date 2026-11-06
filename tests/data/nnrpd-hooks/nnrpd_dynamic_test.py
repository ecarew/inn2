#  nnrpd Python dynamic access hook for support/live-innd.sh
#  (python_dynamic: "nnrpd_dynamic_test" in readers.conf;
#  doc/pod/hook-python.pod).  Allows reading any group and refuses posting.

from nnrpd import set_auth_hook


class Dynamic:
    def dynamic_init(self):
        pass

    def dynamic(self, attributes):
        if attributes["type"].tobytes() == b"post":
            return "No posting through this server"
        return None

    def dynamic_close(self):
        pass


set_auth_hook(Dynamic())
