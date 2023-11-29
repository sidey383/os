#define _GNU_SOURCE
#include <pthread.h>
#include <assert.h>

#include "queue.h"

void *qmonitor(void *arg) {
    queue_t *q = (queue_t *)arg;

    printf("qmonitor: [%d %d %d]\n", getpid(), getppid(), gettid());

    while (1) {
        queue_print_stats(q);
        sleep(1);
    }

    return NULL;
}

queue_t* queue_init(int max_count) {
    int err;

    queue_t *q = malloc(sizeof(queue_t));
    if (!q) {
        printf("Cannot allocate memory for a queue\n");
        abort();
    }

    if(0 != sem_init(&(q->lock), 0, 1)) {
        printf("queue_init: sem_init() failed: %s\n", strerror(errno));
        free(q);
        abort();
    }

    q->first = NULL;
    q->last = NULL;
    q->max_count = max_count;
    q->count = 0;

    q->add_attempts = q->get_attempts = 0;
    q->add_count = q->get_count = 0;

    err = pthread_create(&q->qmonitor_tid, NULL, qmonitor, q);
    if (err) {
        printf("queue_init: pthread_create() failed: %s\n", strerror(err));
        abort();
    }

    return q;
}

void queue_destroy(queue_t *q) {
    int err;
    err = pthread_cancel(q->qmonitor_tid);
    if (err != 0) {
        fprintf(stderr, "qmonitor thread cancel error: %s\n", strerror(err));
    } else {
        err = pthread_join(q->qmonitor_tid, NULL);
        if (err != 0)
            fprintf(stderr, "qmonitor thread join error: %s\n", strerror(err));
    }
    while (q->first != NULL) {
        qnode_t *first = q->first;
        q->first = q->first->next;
        free(first);
    }
    if (0 != sem_destroy(&(q->lock))) {
        fprintf(stderr, "queue_destroy: sem_destroy() failed: %s\n", strerror(errno));
        free(q);
    }
}
int queue_add(queue_t *q, int val) {
    if (0 != sem_wait(&(q->lock))) {
        fprintf(stderr, "queue_add: sem_wait() failed: %s\n", strerror(errno));
        return ERROR;
    }
    q->add_attempts++;

    assert(q->count <= q->max_count);

    if (q->count == q->max_count) {
        if (0 != sem_post(&(q->lock))) {
            fprintf(stderr, "queue_add: sem_post() failed: %s\n", strerror(errno));
        }
        return ERROR;
    }

    qnode_t *new = malloc(sizeof(qnode_t));
    if (!new) {
        printf("Cannot allocate memory for new node\n");
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
    if (0 != sem_post(&(q->lock))) {
        fprintf(stderr, "queue_add: sem_post() failed: %s\n", strerror(errno));
    }
    return OK;
}

int queue_get(queue_t *q, int *val) {
    if (0 != sem_wait(&(q->lock))) {
        fprintf(stderr, "queue_get: sem_wait() failed: %s\n", strerror(errno));
        return ERROR;
    }
    q->get_attempts++;

    assert(q->count >= 0);

    if (q->count == 0) {
        if (0 != sem_post(&(q->lock))) {
            fprintf(stderr, "queue_get: sem_post() failed: %s\n", strerror(errno));
        }
        return ERROR;
    }

    qnode_t *tmp = q->first;

    *val = tmp->val;
    q->first = q->first->next;

    free(tmp);
    q->count--;
    q->get_count++;

    if (0 != sem_post(&(q->lock))) {
        fprintf(stderr, "queue_get: sem_post() failed: %s\n", strerror(errno));
    }
    return OK;
}

void queue_print_stats(queue_t *q) {
    if (0 != sem_wait(&(q->lock))) {
        fprintf(stderr, "queue_get: sem_wait() failed: %s\n", strerror(errno));
        return;
    }
    printf("queue stats: current size %d; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
            q->count,
            q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,
            q->add_count, q->get_count, q->add_count -q->get_count);
    if (0 != sem_post(&(q->lock))) {
        fprintf(stderr, "queue_print_stats: sem_post() failed: %s\n", strerror(errno));
    }
}

