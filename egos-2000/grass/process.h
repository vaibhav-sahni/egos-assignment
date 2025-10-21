#pragma once

#include "egos.h"
#include "syscall.h"

enum proc_status {
    PROC_UNUSED,
    PROC_LOADING,
    PROC_READY,
    PROC_RUNNING,
    PROC_RUNNABLE,
    PROC_PENDING_SYSCALL
};

#define MAX_NPROCESS        16
#define SAVED_REGISTER_NUM  32
#define SAVED_REGISTER_SIZE SAVED_REGISTER_NUM * 4
#define SAVED_REGISTER_ADDR (void*)(EGOS_STACK_TOP - SAVED_REGISTER_SIZE)

/**
 * struct process - per-process control block (PCB)
 *
 * Notes / Contracts:
 *  - This structure lives in the Grass kernel and represents both kernel and
 *    user processes (user pids >= GPID_USER_START). Fields must be kept small
 *    and fixed-size because this code runs in kernel context.
 *  - Time values are measured in microseconds using mtime_get().
 *  - mlfq_level follows: 0 (highest priority) .. (MLFQ_NLEVELS-1) (lowest).
 *  - mlfq_runtime accumulates runtime used at the current level and is
 *    compared against MLFQ_LEVEL_RUNTIME(level) to determine demotion.
 *  - sleep_until, if non-zero, indicates the process should be skipped by the
 *    scheduler until that time.
 */
struct process {
    int pid;                      /* assigned process id */
    struct syscall syscall;       /* in-kernel copy of user syscall args */
    enum proc_status status;      /* current lifecycle state */
    uint mepc;                    /* saved program counter for mret */
    uint saved_registers[SAVED_REGISTER_NUM];

    /* Lifecycle statistics (for assignment grading and debugging) */
    ulonglong create_time;       /* time when proc_alloc() was called */
    ulonglong first_scheduled;   /* time when the process was first scheduled (response) */
    ulonglong cpu_time;          /* aggregated CPU time accumulated (usec) */
    uint timer_interrupts;       /* number of timer interrupts observed */

    /* MLFQ scheduling fields */
    int mlfq_level;              /* 0 (highest) .. 4 (lowest) */
    ulonglong mlfq_runtime;      /* runtime accumulated at current level (usec) */

    /* Sleep and scheduling bookkeeping */
    ulonglong sleep_until;       /* if non-zero, skip scheduling until this absolute time */
    ulonglong last_scheduled;    /* timestamp when the proc was switched to RUNNING */
};

ulonglong mtime_get();
void core_set_idle(uint);

int proc_alloc();
void proc_free(int);
void proc_set_ready(int);
void proc_set_running(int);
void proc_set_runnable(int);
void proc_set_pending(int);

void mlfq_reset_level();
void mlfq_update_level(struct process* p, ulonglong runtime);
void proc_sleep(int pid, uint usec);
void proc_coresinfo();
