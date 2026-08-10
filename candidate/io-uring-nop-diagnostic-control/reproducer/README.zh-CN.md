# Reproducer

[`io_uring_nop_round.c`](io_uring_nop_round.c) 是正式测量使用的原样 raw-UAPI workload，
不依赖 liburing。它提供 `plain`、`inject`、`formal` 和非计时 `v7-semantic` profile；
ring 建立和清理位于计时区外，逐轮验证 CQE、提交/完成数、overflow、残留请求和 affinity。

```bash
make
NOP_EXECUTE_ACK=YES \
  ./run_nop.sh --profile plain --phase smoke --out-dir out
```

[`run_nop.sh`](run_nop.sh) 提供固定入口，[`validate_nop_tsv.py`](validate_nop_tsv.py)
验证 TSV 合同。正式 `formal/point` 运行还要求设置 `NOP_TIMING_ACK=YES` 以及三项
`NOP_REQUIRED_*_PREEMPT` 合同；runner 会在开始计时前硬检查实际 preempt、CPU 策略和
内核身份。ring entries、QD、批数和 warm-up/measured 轮数已冻结在源码中，不接受运行时
参数扫描。
