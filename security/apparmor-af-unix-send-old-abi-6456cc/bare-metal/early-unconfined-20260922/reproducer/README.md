# Follow-up workload and permission helpers

These three files are unchanged copies of the tested inputs:

- `af_unix_live.c`: two-process abstract AF_UNIX datagram workload; CPUs 2/4,
  128-byte messages, batch 32. `timing` uses 3 warm-up and 15 measured rounds;
  `probe` uses 1,024 messages without warm-up. Only sends are timed.
- `checks.py`: single-label policy setup, retained-socket policy replacement,
  bounded concurrency checks, and four static probe-sized smoke cases.
- `permission_checks.py`: the separate 29 static permission controls, including
  stacked labels and old/new ABIs. It is not the concurrent-update test.

The [original socketpair workload](../../../reproducer/af_unix_sendmmsg_standalone.c)
is reused, not duplicated here. Build the added native program with:

```sh
cc -O2 -g -Wall -Wextra -o af_unix_live af_unix_live.c -ldl
```

Use a dedicated test machine with AppArmor, `libapparmor.so.1`,
`/usr/sbin/apparmor_parser`, ABI files `5.0` and `kernel-5.4-vanilla`, and enabled
`network_v9/af_unix`. CPU 2 and CPU 4 must be available. The controller starts
unconfined as root only to manage its temporary policies and enter a label;
the two-process workload drops to UID/GID 1000 before creating sockets.

The following commands load and remove only the helpers' prefixed test policies.
Use fresh output directories and do not run them alongside real measurements:

```sh
sudo python3 permission_checks.py --output /tmp/aa-static-review
sudo python3 checks.py --output /tmp/aa-live-review
```

The second command checks correctness and runs small smokes; it does not perform
the formal timing matrix. Its `Policies`, `CASES` and `native_run` helpers expose
the same policy setup and native timing calls used in the experiment. For one
boot, the formal collection order is three repetitions of the four cases:

```python
from pathlib import Path
import checks

base = Path.cwd()
out = Path('/tmp/aa-timing-review')
out.mkdir()  # must be new
policies = checks.Policies(out)
try:
    for name in checks.PROFILES:
        policies.load(name)
    for repetition in range(3):
        for case in checks.CASES:
            checks.native_run(base, out / f'timing-{repetition}-{case}', case, 'timing')
finally:
    policies.close()
```

This snippet also needs a root controller and a prebuilt `af_unix_live` in the
current directory. It does not install/reboot kernels, set power policies, or
certify build/runtime equivalence. Match the settings in the parent README,
run correctness and untimed probes separately, then collect clean timing on
independent original → patched → original boots. Do not mix probe output,
warm-ups, different workload shapes, or different preemption modes.
