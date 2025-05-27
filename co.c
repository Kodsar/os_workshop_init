#include "co.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdatomic.h>
#include <signal.h>
#include <pthread.h>


static task_t task_queue[TASK_QUEUE_SIZE];
static int front = 0;   
static int rear = 0;   

//one task at a time
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;


static pthread_t workers[WORKER_COUNT];

static atomic_bool running = false;

void *co_worker(void *arg) {
    (void)arg; 

    while (atomic_load(&running)) {
        pthread_mutex_lock(&queue_lock);

        while (front == rear && atomic_load(&running)) {
            pthread_cond_wait(&queue_not_empty, &queue_lock);
        }

        if (!atomic_load(&running)) {
            pthread_mutex_unlock(&queue_lock);
            break;
        }

        task_t t = task_queue[front];
        front = (front + 1) % TASK_QUEUE_SIZE;

        pthread_mutex_unlock(&queue_lock);

        t.func(t.arg);
    }

    return NULL;
}

void co_init() {
    atomic_store(&running, true);

    for (int i = 0; i < WORKER_COUNT; i++) {
        if (pthread_create(&workers[i], NULL, co_worker, NULL) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }
}

void co_shutdown() {
    atomic_store(&running, false);

    pthread_mutex_lock(&queue_lock);
    pthread_cond_broadcast(&queue_not_empty);
    pthread_mutex_unlock(&queue_lock);

    for (int i = 0; i < WORKER_COUNT; i++) {
        pthread_join(workers[i], NULL);
    }
}

void co(task_func_t func, void *arg) {
    pthread_mutex_lock(&queue_lock);

    int next_rear = (rear + 1) % TASK_QUEUE_SIZE;

    if (next_rear == front) {
        fprintf(stderr, "Task queue is full! Task dropped.\n");
        pthread_mutex_unlock(&queue_lock);
        return;
    }

    task_queue[rear].func = func;
    task_queue[rear].arg = arg;
    rear = next_rear;

    pthread_cond_signal(&queue_not_empty);
    pthread_mutex_unlock(&queue_lock);
}



int wait_sig() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);  // Block signals so they are handled by sigwait
    printf("Waiting for SIGINT (Ctrl+C) or SIGTERM...\n");
    int signum;
    sigwait(&mask, &signum);  // Wait for a signal
    printf("Received signal %d, shutting down...\n", signum);
    return signum;
}
