#  An innd Python filter for the filter tests: the rules of
#  tests/data/filter_innd_spam.pl, so both languages are checked against the
#  same articles.  innd loads it as filter_innd.py from pathfilter
#  (innd/python.cc, doc/pod/hook-python.pod); header fields arrive as
#  memoryview objects, or None when the article has no such field.
#
#  filter_art returns the empty string to accept the article, or the reason
#  to reject it.

import re

from INN import set_filter_hook

BINARY = re.compile(r"vset|yEnc|part \d+", re.IGNORECASE)
BLOCKED = re.compile(r"roguespammer\.example|badrelay\.example", re.IGNORECASE)


def text(value):
    """A header field's value as str, or None."""
    if value is None:
        return None
    return value.tobytes().decode("utf-8", errors="backslashreplace")


class SpamFilter:
    def filter_art(self, art):
        groups = text(art["Newsgroups"])
        subject = text(art["Subject"])
        path = text(art["Path"])
        sender = text(art["From"])

        # 1. Excessive cross-posting: more than 5 newsgroups.
        if groups is not None and len(groups.split(",")) > 5:
            return "Excessive cross-posting is limited to 5 groups max"

        # 2. Binaries outside binaries groups.
        if subject is not None and groups is not None:
            if BINARY.search(subject) and not re.search(
                "binar", groups, re.IGNORECASE
            ):
                return "Binary attachments are not allowed in text hierarchies"

        # 3. Known spam injection hosts in the Path header field.
        if path is not None and BLOCKED.search(path):
            return "Article injected via a blocked transit host"

        # 4. A From header field is required.
        if not sender:
            return "Article missing From header field"

        return ""

    def filter_messageid(self, msgid):
        return ""

    def filter_mode(self, oldmode, newmode, reason):
        pass


set_filter_hook(SpamFilter())
