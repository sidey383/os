#define _GNU_SOURCE

#include <pthread.h>
#include <assert.h>

#include "queue.h"

void *qmonitor(void *arg) {
    queue_t *q = (queue_t *) arg;

    printf("qmonitor: [%d %d %d]\n", getpid(), getppid(), gettid());

    while (1) {
        queue_print_stats(q);
        sleep(1);
    }

    return NULL;
}

queue_t *queue_init(int max_count) {
    int err;

    queue_t *q = malloc(sizeof(queue_t));
    if (!q) {
        printf("Cannot allocate memory for a queue\n");
        abort();
    }
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    //PTHREAD_MUTEX_FAST_NP
    //PTHREAD_MUTEX_RECURSIVE_NP
    //PTHREAD_MUTEX_ERRORCHECK_NP
    // Always return 0 with argument PTHREAD_MUTEX_ERRORCHECK_NP
    pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK_NP);
    // Always return 0 with argument PTHREAD_PROCESS_PRIVATE
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
    // Always return 0
    pthread_mutex_init(&(q->lock), &mattr);
    // Always return 0
    pthread_mutexattr_destroy(&mattr);

    pthread_condattr_t attr;
    // Always return 0
    pthread_condattr_init(&attr);
    //pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    //pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE)
    // Always return 0
    pthread_cond_init(&(q->cond_r), &attr);
    pthread_cond_init(&(q->cond_w), &attr);
    pthread_condattr_destroy(&attr);

    q->first = NULL;
    q->last = NULL;
    q->max_count = max_count;
    q->count = 0;

    q->add_attempts = q->get_attempts = 0;
    q->add_count = q->get_count = 0;

    err = pthread_create(&q->qmonitor_tid, NULL, qmonitor, q);
    if (err) {
        printf("queue_init: pthread_create() failed: %s\n", strerror(err));
        err = pthread_mutex_destroy(&(q->lock));
        if (err != 0)
            fprintf(stderr, "queue lock destroy error: %s\n", strerror(errno));
        err = pthread_cond_destroy(&(q->cond_r));
        if (err != 0)
            fprintf(stderr, "queue cond var destroy error: %s\n", strerror(errno));
        err = pthread_cond_destroy(&(q->cond_w));
        if (err != 0)
            fprintf(stderr, "queue cond var destroy error: %s\n", strerror(errno));
        free(q);
        abort();
    }

    return q;
}

void queue_destroy(queue_t *q) {
    int err;
    err = pthread_cancel(q->qmonitor_tid);
    if (err != 0) {
        fprintf(stderr, "qmonitor thread cancel error: %d\n", err);
        err = 0;
    } else {
        err = pthread_join(q->qmonitor_tid, NULL);
    }
    if (err != 0)
        fprintf(stderr, "qmonitor thread join error: %d\n", err);
    while (q->first != NULL) {
        qnode_t *first = (qnode_t *) q->first;
        q->first = q->first->next;
        free(first);
    }
    err = 0;
    do {
        if (err == EBUSY)
            sched_yield();
        err = pthread_mutex_destroy(&(q->lock));
    } while (err == EBUSY);
    if (err != 0)
        fprintf(stderr, "queue lock destroy error: %s\n", strerror(errno));
    err = pthread_cond_destroy(&(q->cond_r));
    if (err != 0)
        fprintf(stderr, "queue cond var read destroy error: %s\n", strerror(errno));
    err = pthread_cond_destroy(&(q->cond_w));
    if (err != 0)
        fprintf(stderr, "queue cond var read destroy error: %s\n", strerror(errno));
    free(q);
}

int queue_add(queue_t *q, int val) {
    int fatalErr;
    int isFull;
    fatalErr = pthread_mutex_lock(&(q->lock));
    if (fatalErr != 0) {
        fprintf(stderr, "pthread_mute_lock() error: %s\n", strerror(fatalErr));
        return FATAL;
    }
    q->add_attempts++;

    isFull = q->count >= q->max_count;
    while (isFull) {
        fatalErr = pthread_cond_wait(&(q->cond_w), &(q->lock));
        if (fatalErr != 0) {
            fprintf(stderr, "pthread_cond_wait() error: %s\n", strerror(fatalErr));
            return FATAL;
        }
        isFull = q->count >= q->max_count;
    }

    qnode_t *new = malloc(sizeof(qnode_t));
    if (new == NULL) {
        fprintf(stderr, "Cannot allocate memory for new node\n");
        abort();
    }

    new->val = val;
    new->next = NULL;

    if (!q->first) {
        q->first = q->last = new;
    } else {
        q->last->next = new;
        q->last = q->last->next;
    }

    q->count++;
    q->add_count++;
    // Never return an error code
    pthread_cond_signal(&(q->cond_r));
    fatalErr = pthread_mutex_unlock(&(q->lock));
    if (fatalErr != 0) {
        fprintf(stderr, "Cannot unlock mutex %s\n", strerror(errno));
        return FATAL;
    }
    return OK;
}

int queue_get(queue_t *q, int *val) {
    int fatalErr;
    int isEmpty;
    fatalErr = pthread_mutex_lock(&(q->lock));
    if (fatalErr != 0) {
        fprintf(stderr, "pthread_mute_lock() error: %s\n", strerror(fatalErr));
        return FATAL;
    }
    q->get_attempts++;

    isEmpty = q->count == 0;
    while (isEmpty) {
        fatalErr = pthread_cond_wait(&(q->cond_r), &(q->lock));
        if (fatalErr != 0) {
            fprintf(stderr, "pthread_cond_wait() error: %s\n", strerror(fatalErr));
            return FATAL;
        }
        isEmpty = q->count == 0;
    }

    qnode_t *tmp = (qnode_t *) q->first;

    *val = tmp->val;
    q->first = q->first->next;

    free(tmp);
    q->count--;
    q->get_count++;

    pthread_cond_signal(&(q->cond_w));
    fatalErr = pthread_mutex_unlock(&(q->lock));
    if (fatalErr != 0) {
        fprintf(stderr, "Cannot unlock mutex %s\n", strerror(errno));
        return FATAL;
    }
    return OK;
}

void queue_print_stats(queue_t *q) {
    if (pthread_mutex_lock(&(q->lock)) != 0) {
        return;
    }
    printf("queue stats: current size %d; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
           q->count,
           q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,
           q->add_count, q->get_count, q->add_count - q->get_count);
    if (pthread_mutex_unlock(&(q->lock)) != 0) {
        fprintf(stderr, "Cannot unlock mutex %s\n", strerror(errno));
    }
}

