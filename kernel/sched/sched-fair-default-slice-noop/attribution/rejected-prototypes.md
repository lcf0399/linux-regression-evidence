# Why the first two prototypes were retired

These failures explain why the later checks were necessary. Their performance, if any, must not be assigned to v3 or v4.

| Date | Prototype | Evidence | Disposition |
| --- | --- | --- | --- |
| October 7 | First version | A source-level DL-PI counterexample changes EBUSY into success by confusing stored policy with the actual scheduling class. This review did not reproduce that PI state on the running kernel. | Not suitable for submission; add an actual-class guard. |
| October 7 | Second version | Bare-metal RT-PI test: after unboosting and a default-runtime legacy call, original uclamp_min resets to 0 but v2 leaves 1024. | Correctness failed; remaining v2 cases stopped and no v2 timing was collected. |

V3 handles the tested default-runtime reset, but later review exposes the two inherited **custom equal-runtime** cases documented in the October 12 boundary batch. V4 then addresses those cases. Earlier failure records remain in the research workspace; retired patch/build files are not reintroduced here.
