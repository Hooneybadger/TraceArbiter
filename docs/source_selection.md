# Source selection

Declared in `configs/priorities.yaml`. Availability is discovered
from the host, not from the file.

| Source | Class | Why it is in the set |
|---|---|---|
| `sched:sched_switch` | critical | Scheduler context-switch stream |
| `sched:sched_waking` | critical | Wakeup path paired with switch |
| `sched:sched_wakeup` | critical | Wakeup path paired with switch |
| `sched:sched_process_exit` | useful | Process lifetime, low rate |
| `exceptions:page_fault_user` | useful | Memory pressure / paging |
| `raw_syscalls:sys_enter` | bulk | High-rate verbose stream |

Priority is a diagnosis goal, not measured importance. Do not infer
it with ML.

B2 static split always puts bulk sources on a `bulk` instance with
30% of the buffer and everything else on `critical` with 70%. That
split is human-authored and is not updated from calibration.

Holdout: B2 is the control that keeps the **same six sources** as
B1. It did not produce the large overrun reductions. Those appear
only when B3 **does not admit** `raw_syscalls:sys_enter`.
