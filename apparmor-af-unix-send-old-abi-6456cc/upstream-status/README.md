# Upstream status audit

Audit date: 2026-08-05.

- The exact introducing commit and the AppArmor 6.17 merge history were
  checked.
- Later history for `security/apparmor/af_unix.c` and
  `security/apparmor/include/af_unix.h` was reviewed through current Linus
  master. No later change was found that clearly removes this measured path or
  provides an equivalent performance fix.
- Searches for the exact commit, subject, and AF_UNIX/AppArmor send-performance
  terms found no matching performance report.
- No standalone public patch thread for the exact commit was identified, so
  the prepared message is a new narrow regression report rather than a reply.

Current Linus master `0d8395707651` `scripts/get_maintainer.pl` routing for the
two changed files:

- To: John Johansen `<john.johansen@canonical.com>` and Georgia Garcia
  `<georgia.garcia@canonical.com>`;
- Cc: Paul Moore `<paul@paul-moore.com>`, James Morris
  `<jmorris@namei.org>`, Serge E. Hallyn `<serge@hallyn.com>`,
  `apparmor@lists.ubuntu.com`, `linux-security-module@vger.kernel.org`,
  `linux-kernel@vger.kernel.org`, and `regressions@lists.linux.dev`.

The audit does not claim that current master was remeasured; the exact A/B is
bound to the isolated source delta described in this bundle.
