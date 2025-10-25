/*
 * (C) 2025, Cornell University
 * All rights reserved.
 *
 * Description: helper functions for process management
 */

#include "process.h"

#define MLFQ_NLEVELS          5
#define MLFQ_RESET_PERIOD     10000000         /* 10 seconds */
#define MLFQ_LEVEL_RUNTIME(x) (((x) + 1) * 100000) /* e.g., 100ms for level 0 */
extern struct process proc_set[MAX_NPROCESS + 1];

static void proc_set_status(int pid, enum proc_status status) {
    for (uint i = 0; i < MAX_NPROCESS; i++)
        if (proc_set[i].pid == pid) proc_set[i].status = status;
}

/* Small helpers to change process status. Kept simple because these are
 * called from various contexts (interrupts, kernel entry). The proc_set_status
 * scanner finds the PCB by pid and updates the status field. */
void proc_set_ready(int pid) { proc_set_status(pid, PROC_READY); }
void proc_set_running(int pid) { proc_set_status(pid, PROC_RUNNING); }
void proc_set_runnable(int pid) { proc_set_status(pid, PROC_RUNNABLE); }
void proc_set_pending(int pid) { proc_set_status(pid, PROC_PENDING_SYSCALL); }

int proc_alloc() {
    static uint curr_pid = 0;
    for (uint i = 0; i < MAX_NPROCESS; i++)
        if (proc_set[i].status == PROC_UNUSED) {
            proc_set[i].pid    = ++curr_pid;
            proc_set[i].status = PROC_LOADING;
            /* Initialize lifecycle and scheduling bookkeeping:
             *  - create_time: record creation timestamp for turnaround calc
             *  - first_scheduled: filled when process is scheduled first time
             *  - cpu_time & timer_interrupts: accumulated during runtime
             *  - mlfq_level/mlfq_runtime: MLFQ priority and runtime accounting
             *  - sleep_until: used by proc_sleep() to temporarily skip scheduling
             */
            proc_set[i].create_time = mtime_get();
            proc_set[i].first_scheduled = 0;
            proc_set[i].cpu_time = 0;
            proc_set[i].timer_interrupts = 0;
            proc_set[i].mlfq_level = 0;
            proc_set[i].mlfq_runtime = 0;
            proc_set[i].sleep_until = 0;
            proc_set[i].last_scheduled = 0;

            /* Student's code ends here. */
            return curr_pid;
        }

    FATAL("proc_alloc: reach the limit of %d processes", MAX_NPROCESS);
}

void proc_free(int pid) {
    /* When a process is freed we print lifecycle statistics for debugging and
     * grading. The metrics printed are:
     *  - turnaround = now - create_time
     *  - response = first_scheduled - create_time (0 if never scheduled)
     *  - cpu = cpu_time (accumulated committed CPU time in microseconds)
     *  - timer_intr = timer_interrupts (number of timer interrupts handled)
     *  - final_level = mlfq_level (priority level at termination)
     *
     * Edge cases:
     *  - If the process was never scheduled (first_scheduled==0), response is
     *    reported as 0.
     *  - GPID_ALL is used to free all user processes; we iterate and print
     *    stats for each user PID present in proc_set.
     */

    /* Print the lifecycle statistics of the terminated process or processes. */
    if (pid != GPID_ALL) {
        /* Find the process entry and print stats if available. */
        for (uint i = 0; i < MAX_NPROCESS; i++)
            if (proc_set[i].pid == pid) {
                ulonglong now = mtime_get();
                ulonglong turnaround = now - proc_set[i].create_time;
                ulonglong response = proc_set[i].first_scheduled
                                          ? proc_set[i].first_scheduled - proc_set[i].create_time
                                          : 0;
                INFO("proc %d: turnaround=%llu usec, response=%llu usec, cpu=%llu usec, timer_intr=%u, final_level=%d",
                     pid, turnaround, response, proc_set[i].cpu_time,
                     proc_set[i].timer_interrupts, proc_set[i].mlfq_level);

                earth->mmu_free(pid);
                proc_set[i].status = PROC_UNUSED;
                break;
            }
    } else {
        /* Free all user processes. */
        for (uint i = 0; i < MAX_NPROCESS; i++)
            if (proc_set[i].pid >= GPID_USER_START &&
                proc_set[i].status != PROC_UNUSED) {
                ulonglong now = mtime_get();
                ulonglong turnaround = now - proc_set[i].create_time;
                ulonglong response = proc_set[i].first_scheduled
                                          ? proc_set[i].first_scheduled - proc_set[i].create_time
                                          : 0;
                INFO("proc %d: turnaround=%llu usec, response=%llu usec, cpu=%llu usec, timer_intr=%u, final_level=%d",
                     proc_set[i].pid, turnaround, response, proc_set[i].cpu_time,
                     proc_set[i].timer_interrupts, proc_set[i].mlfq_level);

                earth->mmu_free(proc_set[i].pid);
                proc_set[i].status = PROC_UNUSED;
            }
    }
    /* Student's code ends here. */
}

void mlfq_update_level(struct process* p, ulonglong runtime) {
    /*
     * mlfq_update_level - update runtime accounting and enforce demotion rules
     *
     * Inputs:
     *  - p: pointer to the process struct (must be non-NULL)
     *  - runtime: number of microseconds the process ran since the last update
     *
     * Side effects:
     *  - increments p->cpu_time by runtime
     *  - increments p->mlfq_runtime by runtime
     *  - if p->mlfq_runtime reaches or exceeds the per-level threshold,
     *    the process is demoted to the next lower-priority level and the
     *    consumed runtime for the level is subtracted from p->mlfq_runtime.
     *  - demotion may occur multiple levels if runtime is large enough.
     *
     * Invariants and units:
     *  - All time values are measured in microseconds (mtime_get()).
     *  - MLFQ_LEVEL_RUNTIME(level) yields (level + 1) * 100000 usec (i.e.,
     *    100ms for level 0, 200ms for level 1, ...).
     *
     * Example:
     *  - A process on level 0 uses 250ms (250000 usec) in one shot. The loop
     *    will demote it from level 0 -> level 1 (consuming 100000), then
     *    level 1 -> level 2 (consuming 200000) leaving 0 remaining runtime,
     *    and finally it will end up at level 2.
     */
    if (!p) return;
    p->mlfq_runtime += runtime;
    p->cpu_time += runtime;

    /* If accumulated runtime at this level exceeds the threshold, demote. */
    while (p->mlfq_level < MLFQ_NLEVELS - 1 &&
           p->mlfq_runtime >= MLFQ_LEVEL_RUNTIME(p->mlfq_level)) {
        p->mlfq_runtime -= MLFQ_LEVEL_RUNTIME(p->mlfq_level);
        p->mlfq_level++;
    }
}

void mlfq_reset_level() {
    /*
     * mlfq_reset_level - reset priorities periodically and when shell input
     *
     * Behavior:
     *  - If there is pending keyboard input (earth->tty_input_empty() == false)
     *    the shell process (GPID_SHELL) is boosted to level 0 to improve
     *    responsiveness.
     *  - Additionally, once every MLFQ_RESET_PERIOD microseconds (10 seconds),
     *    all non-unused processes are reset back to level 0 and their
     *    per-level runtime counters are cleared.
     *
     * Edge cases:
     *  - If GPID_SHELL is not present in proc_set (e.g., not spawned yet), the
     *    loop simply does nothing.
     */
    /* Reset shell priority if there is pending keyboard input. */
    if (!earth->tty_input_empty()) {
        for (uint i = 0; i < MAX_NPROCESS; i++)
            if (proc_set[i].pid == GPID_SHELL) {
                proc_set[i].mlfq_level = 0;
                proc_set[i].mlfq_runtime = 0;
            }
    }

    static ulonglong MLFQ_last_reset_time = 0;
    ulonglong now = mtime_get();
    if (MLFQ_last_reset_time == 0) MLFQ_last_reset_time = now;
    if (now - MLFQ_last_reset_time >= MLFQ_RESET_PERIOD) {
        for (uint i = 0; i < MAX_NPROCESS; i++) {
            if (proc_set[i].status != PROC_UNUSED) {
                proc_set[i].mlfq_level = 0;
                proc_set[i].mlfq_runtime = 0;
            }
        }
        MLFQ_last_reset_time = now;
    }

    /* Student's code ends here. */
}

void proc_sleep(int pid, uint usec) {
    /* Set the sleep_until time for the given pid. */
    for (uint i = 0; i < MAX_NPROCESS; i++) {
        if (proc_set[i].pid == pid) {
            proc_set[i].sleep_until = mtime_get() + (ulonglong)usec;
            /* Keep status as RUNNABLE/READY; scheduler will skip until time. */
            return;
        }
    }

    /* Student's code ends here. */
}

void proc_coresinfo() {
    extern uint core_to_proc_idx[NCORES + 1];
    for (uint c = 0; c <= NCORES; c++) {
        uint idx = core_to_proc_idx[c];
        if (idx < MAX_NPROCESS)
            INFO("core %u -> pid %d", c, proc_set[idx].pid);
        else
            INFO("core %u -> idle", c);
    }

    /* Student's code ends here. */
}
