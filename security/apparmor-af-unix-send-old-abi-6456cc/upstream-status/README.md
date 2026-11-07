# Upstream status audit

## 2026-10-19: investigation acknowledged; tracking closed

Regzbot closed this report as inconclusive on October 13, citing a microbenchmark
issue and developer investigation, not a verified fix. See the
[tracking audit](../../../upstream-status/README.md).

John Johansen's [August 5 UTC reply](https://lists.ubuntu.com/archives/apparmor/2026-August/014884.html)
commits to investigating the overhead without promising full recovery. It does
not establish completed analysis. The [September 22 UTC prototype follow-up](https://lists.openwall.net/linux-kernel/2026/09/22/2475)
is Chengfeng's work, not John's fix. No subsequent maintainer technical conclusion
on that prototype appears in the public thread checked for this update.

## Historical August 5–7 snapshot

Source/fix audit date: 2026-08-05. Mail status updated: 2026-08-07.

The regression report was sent on 2026-08-05. John Johansen replied that the
AppArmor maintainers will investigate where the unconfined-path overhead is
coming from. He noted that the referenced commit already appears to contain
an early bailout and said that the current regression should be improvable,
without promising full recovery.

- The exact introducing commit and the AppArmor 6.17 merge history were
  checked.
- Later history for `security/apparmor/af_unix.c` and
  `security/apparmor/include/af_unix.h` was reviewed through current Linus
  master. No later change was found that clearly removes this measured path or
  provides an equivalent performance fix.
- Searches for the exact commit, subject, and AF_UNIX/AppArmor send-performance
  terms found no matching performance report.
- No standalone public patch thread for the exact commit was identified, so
  the report was sent as a new narrow regression report rather than a reply.

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
