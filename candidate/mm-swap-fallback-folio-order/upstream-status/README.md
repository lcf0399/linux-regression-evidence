# Upstream and submission state

Read-only branch refresh on 2026-10-15 confirmed the same heads used in
the preceding source review:

| Tree | Exact head |
| --- | --- |
| Linus master | `0fcb05b72344f40806cbe810de8872f1698bf053` |
| akpm/mm-stable | `2967f3ccdb5fae956f1b085370ec292bab5c7e25` |
| akpm/mm-unstable | `5ba29027115bc6717b8d6dc3296aae0240933811` |

The reviewed `vmscan.c` and `huge_memory.c` snapshots have no equivalent
reclaim-local reordering. Targeted public searches did not identify a
separate posted version of this proposal. This is not an exhaustive
all-branches/all-mail guarantee; repeat the check immediately before sending.

`scripts/get_maintainer.pl` from the fixed MM tree was run on the exact
patch with `--no-git --no-git-fallback`: Andrew Morton, linux-mm and the
MM reviewer set were identified. Draft recipients include relevant reclaim,
THP and swap reviewers; they must be checked again at submission time.

Submission status recorded on 2026-10-17 (UTC+08:00): RFC prepared, **not sent**.
Publishing these measurements does not refresh the dated tree audit above.
No maintainer review tag, merge claim or `Fixes` tag is assigned.
The author explicitly confirmed this contribution's DCO on 2026-10-16;
the corresponding Signed-off-by is now included. `Assisted-by: LLM` and
a plain-language Codex assistance statement are included in the RFC.

The exact measured diff has been retained. The selected submission format is
a small reproducer archive and the patch as attachments, with a fixed public
commit link for configurations, measurements and review notes. Public evidence
and attachment preparation do not mean the email has been sent.
