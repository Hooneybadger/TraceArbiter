# Sources

Primary kernel interface: Linux tracefs (`/sys/kernel/tracing`),
including tracing instances, event enable files, `buffer_size_kb`,
and per-CPU `stats` (`overrun`, `dropped events`, `bytes`,
`entries`). In overwrite mode, `overrun` counts events overwritten
when the buffer is full; `dropped events` counts rejects when
overwrite is off.

Related complementary project:
[CounterBouncer](https://github.com/Hooneybadger/CounterBouncer)
(PMU measurement integrity). That work is separate and is not a
dependency.
