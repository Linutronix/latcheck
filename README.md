# latcheck

The latcheck program is an aid for debugging latency issues in software. It
works by identifying Linux kernel trace event sub-patterns. The sub-patterns
can be combined to form complex patterns that will allow for automated
identification of possible latency problem areas.

These concepts were recently presented at the Real-Time Summit 2016. Here
are links to the
[abstract](https://wiki.linuxfoundation.org/realtime/events/rt-summit2016/pattern-based-failure-analysis),
[slides](https://wiki.linuxfoundation.org/_media/realtime/events/rt-summit2016/rt-debugging-pattern-based-debugging_thomas-gleixner.pdf), and
[video](https://www.youtube.com/watch?v=dLRzMYYr1tc) of that presentation.

## Status

Currently the software provides minimal proof-of-concept tasks. In particular,
the software is able to identify sub-patterns for a single-threaded
application. The purpose at this stage is to validate if sub-patterns can
provide enough insight and if their combination (into complex patterns) can
provide useful information for automated latency hunting software.

The methods and implementation may require significant changes before expanding
this software into a real usable tool. For example, currently sub-patterns are
specified using C structures and functions. This can almost certainly be
implemented using regular expressions in text files, greatly simplifying the
understanding, modification, or addition of sub-patterns.

The software currently pins the evaluated application to CPU0 and only traces
the first task of that application. This was done to minimize the complexity
during this early proof-of-concept phase, though obviously limiting what can be
tested.

## Usage

latcheck makes use of the Linux kernel tracing subsystem to trace an
application. latcheck assumes this is available in `/sys/kernel/debug/tracing`.

To evaluate an application (for example, `sleep`), that application and its
arguments are passed as the arguments to latcheck:

```
sudo ./latcheck sleep 1
```

The output may look something like this:

```
3354.356614 ,----- syscall:in sys_enter: nr=162/nanosleep task=3204 (sleep-3204)
            |     
3354.356626 |  ,-- sched_out_sleeping:in sched_switch: task=3204 (sleep-3204)
            |  |  
3355.356302 |  `-- sched_out_sleeping:out sched_wakeup: task=3204 (<idle>-0)
3355.356302 |  ,-- sched_latency:in sched_wakeup: task=3204 (<idle>-0)
            |  |  
3355.356316 |  `-- sched_latency:out sched_switch: task=3204 (<idle>-0)
            |     
3355.356320 `----- syscall:out sys_exit: nr=162/nanosleep task=3204 (sleep-3204)
```

The number on the left is the timestamp of the event.

Sub-patterns consist of an "in" and an "out" condition. These are connected
using ascii art. In the above example, the following sub-patterns were
identified as significant:

- syscall
- sched_out_sleeping
- sched_latency

The sections highlighted in yellow show where the application was not
scheduled.

Be aware that latcheck only displays what it considers to be significant
sub-pattern matches. See the Sub-Patterns section for details.

## Sub-Patterns

A sub-pattern consists of an "in" and an "out" condition. These conditions are
essentially regular expression matches against lines of a Linux kernel trace.

As an example, one sub-pattern called "sched_latency" begins when a task is
set to runnable via wakeup and spans until that task is activated on the
CPU. A simplified notation for this sub-pattern would be:

```
name: sched_latency
in:   sched_wakeup:*, SAVE=pid
out:  sched_switch:next_pid==SAVE
```

This particular sub-pattern can be seen in the `sleep` example above. The
sub-pattern is implemented in `subpatterns/sched_latency.c`.

See `subpatterns/list.txt` for a list of possible sub-patterns. (Not all of
these have been implemented in latcheck.)

latcheck does not simply perform pattern matching to identify sub-patterns,
but also determines which of the matched sub-patterns are significant. A trace
is evaluated from the perspective of the application being traced. This
application is called the focus task. From this perspective, _significant_
sub-patterns are ones that directly involve scheduling in or scheduling out
the focus task.

In addition, any _relevant_ sub-patterns that overlap, but are not completely
contained within a significant sub-pattern are also significant. Relevant
sub-patterns are ones where either "in" or "out" conditions match against the
focus task.

The reasoning behind the choice of some relevant sub-patterns as significant
is to attempt to isolate the cause for the scheduling. If a relevant
sub-pattern begins and ends without any rescheduling taking place, it is
assumed that sub-pattern is not significant and can be ignored.

## Latency Hunting

It is not yet clear what types of patterns will provide automated
identification of latency problems. As an example we will inspect a simple
example involving IPC via shared memory. A sender with non-RT priority notifies
a high priority receiver (using pthread condvars). However, after notifying
the receiver, the sender sleeps for 1 second before releasing the mutex. From
the sender perspective, latcheck shows the following:

```
6836.442874 ,----------- sched_out_runnable:in sched_switch: task=3724 (send-3724)
            |           
6836.442905 |  ,-------- prio_boost:in sched_pi_setprio: task=3724 prio=0->55 (recv-3721)
            |  |        
6836.442916 `--+-------- sched_out_runnable:out sched_switch: task=3724 (recv-3721)
               |        
6836.442954    |  ,----- syscall:in sys_enter: nr=162/nanosleep task=3724 (send-3724)
               |  |     
6836.442968    |  |  ,-- sched_out_sleeping:in sched_switch: task=3724 (send-3724)
               |  |  |  
6837.442626    |  |  `-- sched_out_sleeping:out sched_wakeup: task=3724 (<idle>-0)
6837.442626    |  |  ,-- sched_latency:in sched_wakeup: task=3724 (<idle>-0)
               |  |  |  
6837.442641    |  |  `-- sched_latency:out sched_switch: task=3724 (<idle>-0)
               |  |     
6837.442648    |  `----- syscall:out sys_exit: nr=162/nanosleep task=3724 (send-3724)
               |        
6837.442660    |  ,----- syscall:in sys_enter: nr=240/futex/FUTEX_UNLOCK_PI task=3724 (send-3724)
               |  |     
6837.442677    `--+----- prio_boost:out sched_pi_setprio: task=3724 prio=55->0 (send-3724)
                  |     
6837.442686       `----- syscall:out sys_exit: nr=240/futex/FUTEX_UNLOCK_PI task=3724 (send-3724)
```

We see that sender was boosted to realtime priority 55 but then called
`nanosleep`, resulting in the 'idle' task being scheduled. 1 second later the
sender task is scheduled again, releases the mutex, and returns to non-RT
priority.

From the perspective of the high priority receiver, latcheck shows the
following:

```
6836.442888 ,----- syscall:in sys_enter: nr=240/futex/FUTEX_LOCK_PI task=3721 (recv-3721)
            |     
6836.442915 |  ,-- sched_out_sleeping:in sched_switch: task=3721 (recv-3721)
            |  |  
6837.442671 |  `-- sched_out_sleeping:out sched_wakeup: task=3721 (send-3724)
6837.442671 |  ,-- sched_latency:in sched_wakeup: task=3721 (send-3724)
            |  |  
6837.442691 |  `-- sched_latency:out sched_switch: task=3721 (send-3724)
            |     
6837.442700 `----- syscall:out sys_exit: nr=240/futex/FUTEX_LOCK_PI task=3721 (recv-3721)
```

From this we see that the receiver tried to acquire the mutex but then was
scheduled out (due to lock contention). 1 second later the receiver was
scheduled again with the mutex acquired.

How could this latency issue be automatically detected? Some possible rules:

- In the case of the non-RT sender, it is probably undesirable to be scheduled
  out in the event of a priority boost. If any patterns consisting of
  "prio_boost:in ... sched_out_*:in ... prio_boost:out" are detected, this
  would identify the undesirable situation.

- In the case of the non-RT sender, it is probably undesirable to be boosted
  for long periods of time. If the task has been boosted for some reason, it
  means it is sitting on a resource that a more important task needs. By
  setting a maximum allowed duration for the prio_boost pattern, this would
  be detected.

- In the case of the high priority receiver, acquiring the lock should always be
  very fast. Otherwise priority inversion most likely is occurring. By setting
  a maximum allowed duration for the syscall:futex/FUTEX_LOCK_PI pattern, this
  would be detected.

By combining these rules and by extending latcheck to communicate with other
latcheck instances, it could be possible to identify overlapping issues between
different tasks. This would further increase the significance of the patterns
found.

## License

latcheck is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

latcheck is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with latcheck.  If not, see <http://www.gnu.org/licenses/>.
