#include "threadtools.h"
#include <sys/signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Print out the signal you received.
 * If SIGALRM is received, reset the alarm here.
 * This function should not return. Instead, call siglongjmp(sched_buf, 1).
 */
void sighandler(int signo) {
    if(signo == SIGTSTP) {
        printf("caught SIGTSTP\n");
    } else if(signo == SIGALRM) {
        printf("caught SIGALRM\n");
        alarm(timeslice);
    }
    sigprocmask(SIG_SETMASK, &base_mask, NULL);
    longjmp(sched_buf, 1);
}

void printrq(void) {
    for(int i = 0; i < rq_size; i++)
        fprintf(stderr, "%d ", ready_queue[i]->id);
    fprintf(stderr, "\n");
}

void printwq(void) {
    for(int i = 0; i < wq_size; i++)
        fprintf(stderr, "%d ", waiting_queue[i]->id);
    fprintf(stderr, "\n");
}

/*
 * Prior to calling this function, both SIGTSTP and SIGALRM should be blocked.
 */
void scheduler() {
    // maintain the waiting queue and the ready queue
    // each time the scheduler is triggered, it decides which thread to run next
    // and brings an available thread from the waiting queue to the ready queue
    // 1. called by main.c.
    int ret = setjmp(sched_buf);
    fprintf(stderr, "========= ENTER SCHEDULER =========\n");
    fprintf(stderr, "%d: Current wq_size: %d\n", ret, wq_size);
    printwq();
    fprintf(stderr, "%d: Current rq_size: %d\n", ret, rq_size);
    printrq();
    fprintf(stderr, "%d: Current rq_current: %d\n", ret, rq_current);
    fprintf(stderr, "===================================\n");
    if(ret == 0) {
        // execute the earliest created thread
        rq_current = 0;
        fprintf(stderr, "[ret:%d] run thread %d\n", ret, RUNNING->id);
        longjmp(ready_queue[rq_current]->environment, 1); // go to the thread
    }
    else {
        // Step 1. 
        // If the lock is available, but the waiting queue is not empty, bring the first thread in the waiting queue 
        // to the ready queue. And that thread should acquire the lock.
        if(bank.lock_owner == -1 && wq_size != 0) {
            struct tcb *t = waiting_queue[0];
            ready_queue[rq_size] = t; // insert at tail of ready queue
            rq_size++;
            bank.lock_owner = t->id;
        // Then, the holes left in the waiting queue should be filled, while keeping the original relative order.
            for(int i = 1; i < wq_size; i++) {
                waiting_queue[i-1] = waiting_queue[i];
            }
            wq_size--;
            fprintf(stderr, "----------------\n");
            fprintf(stderr, "%d: Current wq_size: %d\n", ret, wq_size);
            printwq();
            fprintf(stderr, "%d: Current rq_size: %d\n", ret, rq_size);
            printrq();
            fprintf(stderr, "%d: Current rq_current: %d\n", ret, rq_current);
            fprintf(stderr, "----------------\n");
        }

        // Step 2. Remove the current thread from ready queue
        if(ret == 2) {
            // lock
            struct tcb *t = RUNNING;
            waiting_queue[wq_size] = t;
            wq_size++;

            // 用尾端去遞補
            if(ready_queue[rq_size-1]->id != t->id) {
                // removed thread is not the last one
                RUNNING = ready_queue[rq_size-1];
                rq_size--;
            } else {
                // removed thread is the last one
                rq_size--;
                rq_current = rq_size-1;
                fprintf(stderr, "%d: Current rq_current: %d\n", ret, rq_current);
                RUNNING = ready_queue[rq_size-1];
            }
        }
        else if(ret == 3) {
            // thread_exit
            struct tcb *t = RUNNING; 

            if(ready_queue[rq_size-1]->id != t->id) {
                // removed thread is not the last one
                RUNNING = ready_queue[rq_size-1];
                rq_size--;
            } else {
                // removed thread is the last one
                rq_size--;
                rq_current = rq_size-1;
                fprintf(stderr, "%d: Current rq_current: %d\n", ret, rq_current);
                RUNNING = ready_queue[rq_size-1];
            }
            free(t);
        }

        // Step 3. Switch to the next thread
        if(ret == 1) {
            // thread_yield
            rq_current = (rq_current + 1) % rq_size;
            fprintf(stderr, "%d: Current rq_current: %d\n", ret, rq_current);
            fprintf(stderr, "[ret:%d] run thread %d\n", ret, RUNNING->id);
            longjmp(RUNNING->environment, 1); // go to thread
        } else if (ret == 2 || ret == 3){
            if(wq_size == 0 && rq_size == 0) {
                return;
            }
            fprintf(stderr, "[ret:%d] run thread %d\n", ret, RUNNING->id);
            longjmp(RUNNING->environment, 1);
        }
    }
}

    