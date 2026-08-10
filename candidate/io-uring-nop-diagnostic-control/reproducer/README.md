# Reproducer

[`io_uring_nop_round.c`](io_uring_nop_round.c) is the unchanged raw-UAPI
workload used for formal timing and does not depend on liburing. It provides
`plain`, `inject`, `formal`, and untimed `v7-semantic` profiles. Ring setup and
cleanup are outside the timed region; every round validates CQEs,
submission/completion counts, overflow, residual requests, and affinity.

```bash
make
NOP_EXECUTE_ACK=YES \
  ./run_nop.sh --profile plain --phase smoke --out-dir out
```

[`run_nop.sh`](run_nop.sh) is the fixed entry point and
[`validate_nop_tsv.py`](validate_nop_tsv.py) validates the TSV contract. A
formal `formal/point` run additionally requires `NOP_TIMING_ACK=YES` and the
three `NOP_REQUIRED_*_PREEMPT` contracts; the runner hard-checks actual
preemption mode, CPU policy, and kernel identity before timing. Ring entries,
QD, batch count, and warm-up/measured rounds are frozen in the source and are
not runtime search parameters.
