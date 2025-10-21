/*
/* (C) 2025, Cornell University
 * All rights reserved.
 *
 * Description: kernel interrupt and exception handlers
 *
 * - intr_entry(id): invoked for hardware interrupts (timer/device). For the
 *   preemptive scheduler we use the timer interrupt to measure runtime of the
 *   currently running process, increment its timer interrupt counter, then
 *   call proc_yield() to select the next process to run.
 *
 * - excp_entry(id): invoked for synchronous exceptions and system calls.
 *   System calls are copied from user memory into the kernel PCB and handled
 *   via proc_try_syscall(). Faults should cause termination of the offending
 *   user process (not implemented here; see student hooks).
 */

#include "process.h"
#include <string.h>

uint core_in_kernel;
uint core_to_proc_idx[NCORES + 1];
/* QEMU has cores with ID #1 .. #NCORES. */
/* Hardware has cores with ID #0 .. #NCORES-1. */

struct process proc_set[MAX_NPROCESS + 1];
void core_set_idle(uint core) { core_to_proc_idx[core] = MAX_NPROCESS; }
/* proc_set[MAX_NPROCESS] is a place holder for idle cores. */

#define curr_proc_idx core_to_proc_idx[core_in_kernel]
#define curr_pid      proc_set[curr_proc_idx].pid
#define curr_status   proc_set[curr_proc_idx].status
#define curr_saved    proc_set[curr_proc_idx].saved_registers

static void intr_entry(uint);
static void excp_entry(uint);

void kernel_entry() {
    /* With the kernel lock, only one core can enter this point at any time. */
    asm("csrr %0, mhartid" : "=r"(core_in_kernel));

    /* Save the process context. */
    asm("csrr %0, mepc" : "=r"(proc_set[curr_proc_idx].mepc));
    memcpy(curr_saved, SAVED_REGISTER_ADDR, SAVED_REGISTER_SIZE);

    uint mcause;
    asm("csrr %0, mcause" : "=r"(mcause));
    (mcause & (1 << 31)) ? intr_entry(mcause & 0x3FF) : excp_entry(mcause);

    /* Restore the process context. */
    asm("csrw mepc, %0" ::"r"(proc_set[curr_proc_idx].mepc));
    memcpy(SAVED_REGISTER_ADDR, curr_saved, SAVED_REGISTER_SIZE);
}

#define INTR_ID_TIMER   7
#define EXCP_ID_ECALL_U 8
#define EXCP_ID_ECALL_M 11
static void proc_yield();
static void proc_try_syscall(struct process* proc);

static void excp_entry(uint id) {
    if (id >= EXCP_ID_ECALL_U && id <= EXCP_ID_ECALL_M) {
        /* Copy the system call arguments from user space to the kernel. */
        uint syscall_paddr = earth->mmu_translate(curr_pid, SYSCALL_ARG);
        memcpy(&proc_set[curr_proc_idx].syscall, (void*)syscall_paddr,
               sizeof(struct syscall));
        proc_set[curr_proc_idx].syscall.status = PENDING;

        proc_set_pending(curr_pid);
        proc_set[curr_proc_idx].mepc += 4;
        proc_try_syscall(&proc_set[curr_proc_idx]);
        proc_yield();
        return;
    }
    /* Student's code goes here (System Call & Protection | Virtual Memory). */

    /* Kill the current process if curr_pid is a user application. */

    /* Student's code ends here. */
    FATAL("excp_entry: kernel got exception %d", id);
}

static void intr_entry(uint id) {
    if (id != INTR_ID_TIMER) FATAL("excp_entry: kernel got interrupt %d", id);
    /* Preemptive scheduler accounting for the currently running process:
     *  - Increment the timer interrupt counter (used for statistics).
     *  - Compute the elapsed runtime since last_scheduled and call
     *    mlfq_update_level() to update both cpu_time and mlfq_runtime.
     *  - Update last_scheduled to now so subsequent interrupts measure only
     *    the delta since this interrupt.
     * Note: last_scheduled is set when a process starts running. If it is
     * zero, the process hasn't been started on a core yet and we skip the
     * runtime accounting.
     */
    if (curr_proc_idx < MAX_NPROCESS && proc_set[curr_proc_idx].status == PROC_RUNNING) {
        struct process* p = &proc_set[curr_proc_idx];
        p->timer_interrupts++;
        if (p->last_scheduled) {
            ulonglong now = mtime_get();
            ulonglong runtime = now - p->last_scheduled;
            mlfq_update_level(p, runtime);
            /* update last_scheduled to now so further interrupts measure correctly */
            p->last_scheduled = now;
        }
    }
    proc_yield();
}

static void proc_yield() {
    if (curr_status == PROC_RUNNING) proc_set_runnable(curr_pid);

    /* Student's code goes here (Multiple Projects). */

    /* Before selecting the next process, account for any runtime used by
     * the current process since last_scheduled (this may be the whole time
     * slice or partial if interrupted). Update both cpu_time and mlfq
     * runtime via mlfq_update_level(). Clear last_scheduled; it will be set
     * for the next running process when scheduled below.
     */
    if (curr_proc_idx < MAX_NPROCESS && proc_set[curr_proc_idx].pid != 0) {
        struct process* cur = &proc_set[curr_proc_idx];
        if (cur->last_scheduled) {
            ulonglong now = mtime_get();
            ulonglong runtime = now - cur->last_scheduled;
            /* Update cpu and mlfq accounting for the time slice used. */
            mlfq_update_level(cur, runtime);
            cur->last_scheduled = 0; /* will be set when next scheduled */
        }
    }

    /* [Preemptive Scheduler]
     * Measure and record lifecycle statistics for the *current* process.
     * Modify the loop below to find the next process to schedule with MLFQ.
     * [System Call & Protection]
     * Do not schedule a process that should still be sleeping at this time. */

    /* Find the runnable process with the lowest mlfq_level (highest priority).
     * Tie-breaker: first encountered in round-robin order starting after curr_proc_idx.
     * While scanning we also: (1) invoke proc_try_syscall() for processes in
     * pending-syscall state so they can transition to runnable; (2) skip
     * processes that are sleeping until a future timestamp.
     */
    int next_idx = MAX_NPROCESS;
    int best_level = 5 + 1;
    ulonglong now = mtime_get();
    for (uint i = 1; i <= MAX_NPROCESS; i++) {
        struct process* p = &proc_set[(curr_proc_idx + i) % MAX_NPROCESS];
        if (p->status == PROC_PENDING_SYSCALL) proc_try_syscall(p);

        if (!(p->status == PROC_READY || p->status == PROC_RUNNABLE)) continue;

        /* Skip sleeping processes. */
        if (p->sleep_until && p->sleep_until > now) continue;

        if (p->mlfq_level < best_level) {
            best_level = p->mlfq_level;
            next_idx = (curr_proc_idx + i) % MAX_NPROCESS;
            if (best_level == 0) break; /* can't get higher priority */
        }
    }

    if (next_idx < MAX_NPROCESS) {
        /* Measure and record lifecycle statistics for the next process. */
        struct process* nxt = &proc_set[next_idx];
        if (nxt->first_scheduled == 0) nxt->first_scheduled = now;
        nxt->last_scheduled = now;

        /* [System Call & Protection | Multicore & Locks]
         * Modify mstatus.MPP to enter machine or user mode after mret if needed. */

    } else {
        /* [Multicore & Locks]
         * Release the kernel lock.
         * [Multicore & Locks | System Call & Protection]
         * Set curr_proc_idx to MAX_NPROCESS; Reset the timer;
         * Enable interrupts by setting the mstatus.MIE bit to 1;
         * Wait for the next interrupt using the wfi instruction. */

        FATAL("proc_yield: no process to run on core %d", core_in_kernel);
    }
    /* Student's code ends here. */

    curr_proc_idx = next_idx;
    earth->mmu_switch(curr_pid);
    earth->mmu_flush_cache();
    if (curr_status == PROC_READY) {
        /* Setup argc, argv and program counter for a newly created process. */
        curr_saved[0]                = APPS_ARG;
        curr_saved[1]                = APPS_ARG + 4;
        proc_set[curr_proc_idx].mepc = APPS_ENTRY;
    }
    proc_set_running(curr_pid);
    earth->timer_reset(core_in_kernel);
}

static void proc_try_send(struct process* sender) {
    /*
     * Attempt to deliver a SYS_SEND from 'sender' to its target process.
     *
     * Contract:
     *  - The destination must be present in proc_set and not UNUSED.
     *  - The destination must be currently blocked in SYS_RECV and marked
     *    PENDING. Otherwise the send cannot complete immediately here.
     *  - If receiver declared a specific sender pid, ensure it matches. If
     *    receiver accepts GPID_ALL, any sender is allowed.
     *
     * Side effects:
     *  - Sets dst->syscall.status = DONE and dst->syscall.sender = sender->pid
     *  - Copies message content from sender->syscall.content into dst's syscall
     *  - Does NOT change process statuses here; proc_try_recv will set them
     *    runnable when appropriate.
     */
    for (uint i = 0; i < MAX_NPROCESS; i++) {
        struct process* dst = &proc_set[i];
        if (dst->pid == sender->syscall.receiver &&
            dst->status != PROC_UNUSED) {
            /* Return if dst is not receiving or not taking msg from sender. */
            if (!(dst->syscall.type == SYS_RECV &&
                  dst->syscall.status == PENDING))
                return;
            if (!(dst->syscall.sender == GPID_ALL ||
                  dst->syscall.sender == sender->pid))
                return;

            dst->syscall.status = DONE;
            dst->syscall.sender = sender->pid;
            /* Copy the system call arguments within the kernel PCB. */
            memcpy(dst->syscall.content, sender->syscall.content,
                   SYSCALL_MSG_LEN);
            return;
        }
    }
    FATAL("proc_try_send: unknown receiver pid=%d", sender->syscall.receiver);
}

static void proc_try_recv(struct process* receiver) {
    /* If the receiver is still waiting (PENDING) there is nothing to do.
     * Otherwise the kernel has a completed syscall in receiver->syscall which
     * must be copied back into the user's memory region reserved for syscall
     * arguments. After copying we mark both receiver and the recorded sender
     * runnable so they can make progress in user space.
     */
    if (receiver->syscall.status == PENDING) return;

    /* Copy the system call struct from the kernel back to user space. */
    uint syscall_paddr = earth->mmu_translate(receiver->pid, SYSCALL_ARG);
    memcpy((void*)syscall_paddr, &receiver->syscall, sizeof(struct syscall));

    /* Set the receiver and sender back to RUNNABLE. */
    proc_set_runnable(receiver->pid);
    proc_set_runnable(receiver->syscall.sender);
}

static void proc_try_syscall(struct process* proc) {
    switch (proc->syscall.type) {
    case SYS_RECV:
        proc_try_recv(proc);
        break;
    case SYS_SEND:
        proc_try_send(proc);
        break;
    default:
        FATAL("proc_try_syscall: unknown syscall type=%d", proc->syscall.type);
    }
}

/*
 * proc_try_syscall: dispatcher for in-kernel syscall completion/processing.
 *
 * Note: proc_try_syscall is called from the scheduler code when scanning
 * processes so that sends/receives can be completed opportunistically while
 * the kernel is deciding which process to run next.
 */
