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
        fprintf(stderr, "Cannot allocate memory for a queue\n");
        abort();
    }

    // Always return 0
    pthread_mutex_init(&(q->lock), PTHREAD_PROCESS_PRIVATE);

    q->first = NULL;
    q->last = NULL;
    q->max_count = max_count;
    q->count = 0;

    q->add_attempts = q->get_attempts = 0;
    q->add_count = q->get_count = 0;

    err = pthread_create(&q->qmonitor_tid, NULL, qmonitor, q);
    if (err) {
        fprintf(stderr, "queue_init: monitor thread create failed: %s\n", strerror(err));
        err = pthread_mutex_destroy(&(q->lock));
        if (err != 0) {
            fprintf(stderr, "queue_init: pthread mutex destroy failed: %s\n", strerror(err));
        }
        return NULL;
    }


    return q;
}

void queue_destroy(queue_t *q) {
    int err;
    err = pthread_cancel(q->qmonitor_tid);
    if (err == 0) {
        err = pthread_join(q->qmonitor_tid, NULL);
    } else {
        fprintf(stderr, "qmonitor thread cancel error: %d\n", err);
        err = 0;
    }
    if (err != 0)
        fprintf(stderr, "qmonitor thread join error: %d\n", err);
    while (q->first != NULL) {
        qnode_t *first = q->first;
        q->first = q->first->next;
        free(first);
    }
    err = 0;
    do {
        if (err == EBUSY)
            sched_yield();
        err = pthread_mutex_destroy(&(q->lock));
    } while (err == EBUSY);
    free(q);
}

int queue_add(queue_t *q, int val) {
    int fatalErr;
    int full;
    qnode_t *new;
    fatalErr = pthread_mutex_lock(&(q->lock));
    if (fatalErr != 0) {
        fprintf(stderr, "Can't lock mutex %s", strerror(errno));
        return FATAL;
    }
    q->add_attempts++;

    assert(q->count <= q->max_count);
    full = q->count == q->max_count;
    if (full)
        fatalErr = pthread_mutex_unlock(&(q->lock));
    if (fatalErr != 0) {
        fprintf(stderr, "Can't unlock mutex %s", strerror(errno));
        return FATAL;
    }
    if (full)
        return SIZE_ERR;

    new = malloc(sizeof(qnode_t));
    if (!new) {
        fprintf(stderr, "Cannot allocate memory for new node\n");
        abort();
    }

    new->val = val;
    new->next = NULL;

    if (!q->first)
        q->first = q->last = new;
    else {
        q->last->next = new;
        q->last = q->last->next;
    }

    q->count++;
    q->add_count++;
    fatalErr = pthread_mutex_unlock(&(q->lock));
    if (fatalErr != 0) {
        fprintf(stderr, "Can't unlock mutex %s", strerror(errno));
        return FATAL;
    }
    return OK;
}

int queue_get(queue_t *q, int *val) {
    int fatalErr;
    int empty;
    qnode_t *tmp    ;
    fatalErr = pthread_mutex_lock(&(q->lock));
    if (fatalErr != 0) {
        fprintf(stderr, "Can't lock mutex %s", strerror(errno));
        return FATAL;
    }
    q->get_attempts++;

    assert(q->count >= 0);
    empty = q->count == 0;

    if (empty) {
        fatalErr = pthread_mutex_unlock(&(q->lock));
    }
    if (fatalErr != 0) {
        fprintf(stderr, "Can't unlock mutex %s", strerror(errno));
        return FATAL;
    }
    if (empty) {
        return SIZE_ERR;
    }

    tmp = q->first;

    *val = tmp->val;
    q->first = q->first->next;

    free(tmp);
    q->count--;
    q->get_count++;
    fatalErr = pthread_mutex_unlock(&(q->lock));
    if (fatalErr != 0) {
        fprintf(stderr, "Can't unlock mutex %s", strerror(errno));
        return FATAL;
    }
    return OK;
}

void queue_print_stats(queue_t *q) {
    if (pthread_mutex_lock(&(q->lock)) != 0) {
        fprintf(stderr, "Can't lock mutex %s", strerror(errno));
        return;
    }
    printf("queue stats: current size %d; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
           q->count,
           q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,
           q->add_count, q->get_count, q->add_count - q->get_count);
    if (pthread_mutex_unlock(&(q->lock)) != 0) {
        fprintf(stderr, "Can't unlock mutex %s", strerror(errno));
    }
}

