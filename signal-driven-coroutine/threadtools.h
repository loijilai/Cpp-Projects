#ifndef THREADTOOL
#define THREADTOOL
#include <setjmp.h>
#include <sys/signal.h>
#include "bank.h"


#define THREAD_MAX 16  // maximum number of threads created
#define BUF_SIZE 512
struct tcb {
    int id;  // the thread id
    jmp_buf environment;  // where the scheduler should jump to
    int arg;  // argument to the function
    int i, x, y;  // declare the variables you wish to keep between switches
};



extern int timeslice;
extern jmp_buf sched_buf;
extern struct tcb *ready_queue[THREAD_MAX], *waiting_queue[THREAD_MAX];
extern struct Bank bank;
/*
 * rq_size: size of the ready queue
 * rq_current: current thread in the ready queue
 * wq_size: size of the waiting queue
 */
extern int rq_size, rq_current, wq_size;
/*
* base_mask: blocks both SIGTSTP and SIGALRM
* tstp_mask: blocks only SIGTSTP
* alrm_mask: blocks only SIGALRM
*/
extern sigset_t base_mask, tstp_mask, alrm_mask;
/*
 * Use this to access the running thread.
 */
#define RUNNING (ready_queue[rq_current])

void sighandler(int signo);
void scheduler();

// Call the function func and pass in the arguments id and arg. 
#define thread_create(func, id, arg) {\
    func(id, arg); \
}

// Initialize the thread control block and append it to the ready queue.
// This macro also print a line to the standard output: [thread id] [function name]
// This macro should also call setjmp so the scheduler knows where to longjump when it decides to run the thread.
// Afterwards, it should return the control to main.c.
#define thread_setup(id, arg) {\
    struct tcb *new_thread = (struct tcb *)malloc(sizeof(struct tcb)); \
    new_thread->id = id; \
    new_thread->arg = arg; \
    ready_queue[rq_size] = new_thread; \
    fprintf(stderr, "id: %d, arg: %d\n", id, arg); \
    rq_size++; \
    printf("%d %s\n", id, __func__); \
    if (setjmp(new_thread->environment) == 0) { \
        return; \
    } \
}

// Jump to the scheduler with longjmp(sched_buf, 3).
#define thread_exit() {\
    longjmp(sched_buf, 3); \
}

// After each iteration (step), a thread should use this macro to check if there's a need to let another thread execute.
#define thread_yield() {\
    if(setjmp(RUNNING->environment) == 0) {\
        sigprocmask(SIG_UNBLOCK, &tstp_mask, NULL); \
        sigprocmask(SIG_UNBLOCK, &alrm_mask, NULL); \
        sigprocmask(SIG_SETMASK, &base_mask, NULL); \
    } \
}

#define lock(){\
    if(bank.lock_owner == -1 || bank.lock_owner == RUNNING->id) { \
        fprintf(stderr, "thread id: %d get lock\n", RUNNING->id); \
        bank.lock_owner = RUNNING->id; \
    } \
    else { \
        if(setjmp(RUNNING->environment) == 0) {\
            fprintf(stderr, "thread id: %d fail to get lock\n", RUNNING->id); \
            longjmp(sched_buf, 2); \
        } \
    } \
}

#define unlock() ({\
    if(RUNNING->id == bank.lock_owner) \
        bank.lock_owner = -1; \
})

#endif // THREADTOOL
