DESIGN.md

SimpleOS (MLFQ + shell utilities) — Design Document

Group ID: 6
Members and contributions:
- Member A: Vaibhav Sahni
- Member B: Pramag Basantia

1. Overview

This document describes the changes made to the EGOS-2000 repository in order to
implement the assignment deliverables: two user-space utilities (grep and wcl)
and a Multi-Level Feedback Queue (MLFQ) scheduler in the Grass kernel. The
implementation focuses on clarity and correctness; comments in the code explain
contracts, data layout, and edge cases.

2. High-level changes (files edited/added)

- apps/user/grep.c  — Minimal grep using EGOS syscalls (dir_lookup, file_read).
- apps/user/wcl.c   — Minimal wc -l (counts newlines) using EGOS syscalls.
- grass/process.h   — Extended process control block with lifecycle and MLFQ
                     fields; detailed comments added.
- grass/process.c   — Implemented/annotated MLFQ helpers: proc_alloc,
                     proc_free, mlfq_update_level, mlfq_reset_level, proc_sleep.
- grass/kernel.c    — Kernel interrupt & scheduler hooks annotated; proc_yield
                     selects next process according to MLFQ rules.
- DESIGN.md         — This document (usage and testing instructions below).

3. MLFQ design and mapping to code

Rules implemented (as per assignment spec):
- 5 levels: 0 (highest) .. 4 (lowest). All processes start at level 0.
- A process at level i can run for (i+1)*100 ms before being demoted to level i+1.
- Scheduler always chooses a runnable process with the smallest level number
  (highest priority). Tie-breaking is round-robin starting after the current
  process index.
- Every 10 seconds the scheduler resets all processes to level 0. Additionally,
  if there is keyboard input, the shell (GPID_SHELL) is moved back to level 0.

Code mapping:
- struct process (grass/process.h): fields added
  - mlfq_level, mlfq_runtime — capture MLFQ state and runtime at the level.
  - cpu_time, timer_interrupts, create_time, first_scheduled, last_scheduled —
    lifecycle counters used to print statistics on process termination.
- proc_alloc (grass/process.c): initializes lifecycle and MLFQ fields.
- mlfq_update_level (grass/process.c): increments cpu_time and mlfq_runtime and
  demotes the process to the next level when accumulated runtime exceeds the
  threshold for the current level. It loops in case runtime spans multiple
  demotions.
- mlfq_reset_level (grass/process.c): invoked periodically by scheduler, and
  when keyboard input is pending. Resets appropriate fields.
- intr_entry (grass/kernel.c): on timer interrupt, increments timer_interrupts
  and measures runtime since last_scheduled; calls mlfq_update_level for the
  current process. Afterwards calls proc_yield().
- proc_yield (grass/kernel.c): accounts for the currently running process's
  final runtime slice, then scans processes to find the runnable process with
  lowest mlfq_level and not sleeping. It sets first_scheduled/last_scheduled
  for the selected process and switches context.

4. Shell utilities implementation notes

Both shell apps strictly use EGOS syscall wrappers (`dir_lookup` and
`file_read`) found in `library/syscall/servers.c`. No host libc file APIs are
used.

grep
- Reads file block-by-block, treating each block as a null-terminated string
  for safe local processing.
- Searches for the PATTERN inside each line fragment present in a block and
  prints matching fragments.
- Limitation: a line split across two blocks will not be stitched; thus a
  pattern that crosses the block boundary may not be found. This is acceptable
  for the assignment and for typical text files.

wcl
- Reads files block-by-block and counts '\n' characters; prints the total
  across all files.
- If the last line does not end with '\n', it will not be counted (matching
  counting of newline characters semantics and wc -l behavior).

5. Testing and running

Build:
```
make -C /home/vaibhavsahni/egos/egos-2000
```
Run in QEMU:
```
# from the egos-2000 directory
make qemu
# inside egos, run commands like:
# grep pattern filename
# wcl filename1 filename2
```

6. Further notes and suggested improvements

- Full line reassembly across block boundaries (improvement for grep) — store a
  tail fragment across reads and prepend to the next block.
- Add per-level time quanta tunables and add unit tests / microbenchmarks for
  MLFQ behavior.

7. Contribution and



END
