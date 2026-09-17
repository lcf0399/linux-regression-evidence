# Upstream report status

Status update: 2026-09-17, checked against the author's mailbox thread.
The original report was sent on September 13. Tejun asked about practical
impact; T.J. Mercier supplied the INODE_INITED change. The author's
[validation](../bare-metal/inode-inited/README.md) reply was sent on September 15
(UTC+8). T.J.'s September 15 reply identified synchronization problems and
suggested a watcher-based alternative, while questioning the practical benefit.

An [independent inode-request marker prototype](../bare-metal/inode-requested/README.md)
was tested on September 16. Its results are published; its reply remains a
local, unsent draft. No accepted fix, complete concurrency proof or merge is
claimed. This is a mailbox check, not a new exhaustive public-archive or
mainline merge search. Private messages and drafts are not published here.

Original Message-ID:
`<CANGjgdn=H50AaA-+O_GA-hkE9UwLANmkjx1zRsKUzxF5za8VfQ@mail.gmail.com>`.
Patch reply Message-ID:
`<CABdmKX2Oer9wrxWdR8s48Czro8H51HY+S_mfnUj-RvhjCM1RgA@mail.gmail.com>`.

Author's sent validation reply Message-ID:
`<CANGjgdmKg1_QyzhCSh03T4RStw5_znnhqmFFS8TVwQbm-sZZbA@mail.gmail.com>`.
Latest checked T.J. reply Message-ID:
`<CABdmKX3g7+GTfSBKB42Tr2CDusCNXb5+oPc7TcC1RpVndUzVpw@mail.gmail.com>`.

## Historical source audit: September 13

Public source references were rechecked on 2026-09-13. The mail search below
is the recorded audit from that date, not a fresh mailbox check or an
exhaustive search.

The original notification patch was T.J. Mercier's
`[PATCH v5 2/3] kernfs: Send IN_DELETE_SELF and IN_IGNORED`, Message-ID
`20260225223404.783173-3-tjmercier@google.com`.
Its parent adds protection against resetting a removed directory's link count.
The unmodified public commit descriptions are included as `guard.commit` and
`notify.commit`; see [references](refs.tsv).

Both changes are merged, not pending proposals. They entered mainline through
the `driver-core-7.1-rc1` merge (`4793dae01f47`) on 2026-04-14 and are included
in v7.1 and v7.2. The exact test states use a 7.0-rc3 development base; that
version string does not mean the released v7.0 contains these changes.

The audit pinned Linus mainline to `2f0c1cf72f4682178506f513bbf015e591b1aa4a`,
driver-core-next to `d3bdf70cf0e862d7f1641522c32e55764cd5b481`, and
driver-core-linus to `77be3641f3e3a56e42a5ed889372ef395a933f3c`.
The selected removal/link-count functions still matched the measured notify
state. This is selected-function equality, not equality of whole kernels.

Related work was kept separate:

- `d996995a0ffb86fa0ff0f953221892dced6143a1` simplifies the name hash and is
  in the pinned mainline. Its path was hit; the separate version comparison
  improved about 2%, but does not attribute that gain to this commit alone.
- The [September 5 file-handle decoding fix](https://lists.openwall.net/linux-kernel/2026/09/05/828)
  is not in the tested mainline. It adds locking in `mount.c`; the workload
  does not call `open_by_handle_at()`.
- The [September 10 notification-delivery locking proposal](https://lkml.iu.edu/2609.1/06430.html)
  is not in the tested mainline. It reduces locking around asynchronous
  `kernfs_notify_workfn()` delivery under memory pressure, not this synchronous
  removal chain. An Ack is not a merge confirmation.
- The [September 11 staged sysfs registration RFC](https://lkml.iu.edu/2609.1/11344.html)
  is not in the tested mainline. It batches publication of previously hidden
  device subtrees. This workload instead removes an already published cgroup.

Thus the unmodified pinned 7.3-rc2 test includes the name-hash change, but not
those three proposals. They were not benchmarked at that audit date, so no
claim of zero timing impact was made. The September 14 follow-up explicitly
adds the file-handle read-lock fix to both compared kernels as a prerequisite;
it does not isolate that fix's performance effect.

The bounded search found no matching report of this exact empty-leaf cost.
Some lore pages were unavailable and mirrors were used; access failure is
not evidence that a report does not exist.

The original report opened a narrowly titled regression thread citing the
introducing commits. Follow-up replies stay in that thread. Publication of
evidence and actual email delivery are separate steps. Private mail files
are excluded by `.gitignore`.
