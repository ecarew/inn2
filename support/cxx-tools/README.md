# C++ conversion tools

Helper scripts used to convert INN to C++.  Run them from anywhere; they
locate the repository themselves.  The workflow they support is described in
[CLAUDE.md](../../CLAUDE.md) and the reasoning in
[CONVERSION_NOTES.md](../../CONVERSION_NOTES.md).

| Script | Purpose |
|---|---|
| `convert-file.sh <dir> <path>` | `git mv` one `.c` to `.cc`, update the directory's `CMakeLists.txt`, rename the `class`/`private` keywords in code, build, and list the remaining errors.  Does not commit. |
| `autocast.sh <file>` | Add `static_cast`s for the compiler's "`void *` to `T`" errors in a file. |
| `commit-verified.sh <subject> <body-file> [paths]` | Run `support/verify-cxx.sh` on `build/` (and `build-asan/` if configured) and commit only if everything passes, with the results as `Tested:` sections. |
| `tidy-fix.sh <dir-regex> <check>` | Apply one clang-tidy check's fixes to `.cc` files only, then `clang-format` them.  Does not commit. |
| `check-rename-collision.sh <path> <old> <new>` | Report functions that already use `<new>` before renaming `<old>` to it. |
| `fixkw.pl <file> <keyword> <field-name> <ident-name>` | Rename a C++ keyword used as a name, in code only (never comments or strings): `.kw`/`->kw` become `<field-name>`, other uses `<ident-name>`. |
| `castrhs.pl <file> <line> <type> ...` | Wrap the right-hand side of single-line assignments in `static_cast<type>(...)`. |
| `castmemchr.pl <file> <line> ...` | Wrap `memchr(...)` calls on the given lines in `static_cast<char *>(...)`. |

Typical loop for one file:

```bash
support/cxx-tools/convert-file.sh lib foo
support/cxx-tools/autocast.sh lib/foo.cc      # if it reports void * errors
# fix anything else by hand, then:
support/cxx-tools/commit-verified.sh "Compile lib/foo as C++" body.txt lib/foo.cc lib/CMakeLists.txt
```

Every automatic change still needs review; see CONVERSION_NOTES.md for the
cases where tools produced wrong output.
