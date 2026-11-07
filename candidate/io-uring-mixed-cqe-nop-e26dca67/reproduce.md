# Reproducing the ordinary-CQE comparison

Use the unmodified fio 3.33 `t/io_uring` program used by MMTests. This does not
require a storage device or an application workload. Do not replace it with
a fio job file: the program here is the separate `t/io_uring` binary.

```sh
git clone --branch fio-3.33 --depth 1 https://github.com/axboe/fio.git
cd fio
./configure
make -j4 t/io_uring
./t/io_uring -N 1 -n 20 -r 100
```

Use the same binary on each kernel. The command selects NOP mode, 20
submitter threads and a 100-second run. Defaults are depth 128 and
submission/completion batches of 32/32. Every submitter owns a ring; no
CQE_MIXED, CQE32, IOPOLL or SQPOLL is enabled for this NOP command.
The printed `polled=1` is a program default, not proof of IOPOLL: ring
setup enables IOPOLL only when `polled && !do_nop`.

For the reported result compare, on separate boots:

1. `89a885972140ea68d3f55457d23d0da2350c96ac`.
2. `e26dca67fde194340582cfbb0c0bf661825e9e46`.
3. The same parent image again.

Build both with the same configuration and compiler. The measured pair used
GCC 15.2.0, full preemption, normal security settings and microcode 0x3e.
Only the local-version suffix differed in the configurations. On the
i7-12700KF, all 20 logical CPUs were allowed and Turbo was disabled on all
three boots. The original program did not pin individual workers.

On each boot run one separate 100-second warmup, then nine measured calls
with the identical command. Keep failed calls and their outputs separately;
do not replace them silently. Each successful call reports 99 interval rates;
average those into one call result, then compare the nine call results per
boot. The interval rates are not 99 independent repetitions.

The fixed-duration program's full wall time remained about 100 seconds.
The measured regression is fewer accepted submissions in that duration;
independent initialization and completion checks verified 20 rings and
matching submission/completion counts before clean timing.

No diagnostic prototype is required to reproduce the historical boundary.
The exact pair does not contain later CQE32 boundary fixes; the test uses
ordinary 16-byte completions and does not exercise those wide-CQE cases.
The optional v7.3 comparison prototype is a separate experiment and is not
presented as a production patch or as a precise recovery of the historical loss.

The existing local source/build, raw-output and recovery records remain in
the original experiment (local record);
they are not duplicated into this candidate.
