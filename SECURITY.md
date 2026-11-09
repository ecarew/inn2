# Security Policy

This is the security policy of INN++, the C++ conversion of INN
(see [README.md](README.md)).  INN++ is an independent project, not part
of the InterNetNews project.

## Supported Versions

INN++ is developed on the `cmake-build` branch of
<https://github.com/ecarew/inn2>; releases are tags on that branch.
Security fixes are made on `cmake-build` and appear in the next tag.  Only
the latest tag is supported; please upgrade to it before reporting, since
the issue may already be fixed.

Branch `main` is upstream INN, unmodified, and is not maintained by
INN++.

## Reporting a Vulnerability

Please do **NOT** report security vulnerabilities through public GitHub
issues or pull requests.  Report them **privately** with [GitHub's private
vulnerability reporting](https://github.com/ecarew/inn2/security/advisories/new)
for INN++.  This creates an advisory that only the INN++ maintainers can
see until it is resolved and you agree to disclose it.

**If the vulnerability is also in the original INN** (most of the code is
INN's), please report it to the INN project as well, privately, through the
[GitHub Security Advisories](https://github.com/InterNetNews/inn/security/advisories/)
of the INN project, which only the INN maintainers (Russ Allbery and
Julien Élie) can see.  If you are unsure, report it to INN++ and we will
check the original and pass it on.

When reporting, please include as much of the following as you can:
- a description of the vulnerability and its potential impact (for instance
  remote code execution, control message handling, buffer overrun, SQL
  injection, authentication bypass, denial of service);
- the affected component(s), and the INN++ tag or commit you found it in;
- clear step-by-step instructions to reproduce the issue, or a minimal proof
  of concept, if you have one;
- a patch, if you have one;
- your assessment of severity and any mitigating factors (for instance
  whether exploitation requires a trusted peer feed or an authenticated reader
  connection).

Please do not disclose the issue publicly before a fix is released.

If you have multiple security issues to report, please create a separate report for each vulnerability.

## What to Expect

- We, the INN++ maintainers, will acknowledge your report within a few
  days.
- We will investigate and keep you updated on progress toward a fix.
- Once a fix is ready, we will coordinate a release and a public disclosure
  date with you.
- Credit will be given to you in the release notes and the advisory, unless
  you prefer to remain anonymous.

## Scope

This policy covers the source code in this repository.  Issues in third-party
software that merely integrates with INN++ should be reported to their own
projects.

## The original INN's supported versions

For reference, INN's own policy (as of INN 2.8.0, which INN++ starts from)
supports these branches of the InterNetNews project; they are not INN++
releases:

| Branch | Status | First release | Last release (EOL) | Security support |
| ------ | -------| ------------- | ------------------ | ---------------- |
| main   | Next major version | [Snapshots](https://downloads.isc.org/isc/inn/snapshots/) (2.8.0) | not announced | :white_check_mark: |
| 2.7 | Stable version     | 2022-07-10 (2.7.0) | not announced      | :white_check_mark: (until the 2.8.1 release) |
| 2.6 | Old stable version | 2015-09-12 (2.6.0) | 2022-02-18 (2.6.5) | :x: |
| 2.5 | Old stable version | 2009-05-21 (2.5.0) | 2015-05-23 (2.5.5) | :x: |
| 2.4 | Old stable version | 2003-05-06 (2.4.0) | 2009-02-27 (2.4.6) | :x: |
| 2.3 | Old stable version | 2000-08-20 (2.3.0) | 2003-03-08 (2.3.5) | :x: |
| 2.2 | Old stable version | 1999-01-21 (2.2)   | 2000-07-18 (2.2.3) | :x: |
| 2.1 | Old stable version | 1998-07-24 (2.1)   | 1998-07-24 (2.1)   | :x: |
| 2.0 | Old stable version | 1998-06-08 (2.0)   | 1998-06-08 (2.0)   | :x: |
