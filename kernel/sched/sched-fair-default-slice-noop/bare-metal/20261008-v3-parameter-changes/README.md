# Third prototype parameter-change controls on October 8

This is a separate original v7.2 A → v3 → original v7.2 B test using `sched_setattr()`, not the no-change `sched_setparam()` loop. Mean ns/call:

| Case | Original A | Prototype | Original B | Change vs A/B midpoint |
| --- | ---: | ---: | ---: | ---: |
| Custom 4 ms / 10 ms | 644.520 | 646.424 | 647.087 | +0.096% |
| Default / custom 10 ms | 652.964 | 656.022 | 649.756 | +0.716% |
| Unchanged custom 10 ms control | 189.461 | 191.047 | 191.568 | +0.280% |

The default request is zero, resolving to 2.8 ms in this experiment. The custom-change case alternates 4 and 10 ms; the mode-change case alternates default and custom 10 ms. The third case repeats a custom 10 ms request.

There are 135 measured samples: three cases, three boots, 15 samples per case, with 500,000 calls each and three warm-ups. CPU 2, full preemption, performance governor/EPP and Turbo off; same v7.2/GCC 15.2 pairing as the v3 no-change test. Independent probes and state/permission checks precede clean timing.

All three comparisons meet the recorded stability checks; detailed figures are in [summary.tsv](summary.tsv). The two real-change increases are below 1%, not exactly zero. These are **v3 results only**. No corresponding v4 parameter-change timing is claimed; its custom no-change control uses a different interface and cannot be substituted here.
