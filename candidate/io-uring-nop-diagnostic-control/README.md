# io_uring NOP diagnostic-interface cost

This bundle contains curated bare-metal results and the exact workload for
`io_uring/nop.c`. It has not been sent upstream, and no performance-regression
report is currently recommended: NOP is a control opcode for testing the
io_uring submission/completion framework rather than application data I/O,
and the attributed commit intentionally extends registered-file/buffer test
support.

The release screen used fresh boots in the order
`v6.12.95 A -> v7.1.3 -> v6.12.95 B`. The exact comparison used
`aa00f67adc2c -> a85f31052bce`:

| Comparison | plain NOP | `INJECT_RESULT` | paired ratio |
| --- | ---: | ---: | ---: |
| release | `+15.801%` | `+16.240%` | `+0.378%` |
| exact commit | `+8.311%` | `+8.692%` | `+0.357%` |

Drop-first results preserve both directions. Maximum CV is `0.717%` for the
release screen and `1.707%` for the exact pair. Every comparison point actually
ran `preempt=full`; semantic, CQE, ring-health, and direct-hit checks passed.
The two profiles slow almost equally, locating the signal in their common NOP
path rather than result injection.

The exact commit adds flags, resource checks, and registered-file/buffer test
state, and explains only part of the release gap. Bisecting later diagnostic
feature growth has insufficient real-world value, so this target is closed
under the stop rule.

- [`bare-metal/`](bare-metal/): release and exact summaries, run identities,
  and direct-hit counts;
- [`reproducer/`](reproducer/): the exact raw-UAPI workload, runner, and
  validator.
