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

    err = pthread_mutex_init(&(q->lock), PTHREAD_PROCESS_PRIVATE);
    if (err != 0) {
        fprintf(stderr, "queue mutex init error: %s\n", strerror(errno));
        free(q);
        abort();
    }


    err = pthread_cond_init(&(q->cond_r), NULL);
    if (err != 0) {
        fprintf(stderr, "queue cond var read init error: %s\n", strerror(errno));
        err = pthread_mutex_destroy(&(q->lock));
        if (err != 0) {
            fprintf(stderr, "queue lock destroy error: %s\n", strerror(errno));
        }
        free(q);
        abort();
    }


    err = pthread_cond_init(&(q->cond_w), NULL);
    if (err != 0) {
        fprintf(stderr, "queue cond var write init error: %s\n", strerror(errno));
        err = pthread_mutex_destroy(&(q->lock));
        if (err != 0) {
            fprintf(stderr, "queue lock destroy error: %s\n", strerror(errno));
        }
        err = pthread_cond_destroy(&(q->cond_r));
        if (err != 0) {
            fprintf(stderr, "queue cond var read destroy error: %s\n", strerror(errno));
        }
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
        err = pthread_mutex_destroy(&(q->lock));
        if (err != 0) {
            fprintf(stderr, "queue lock destroy error: %s\n", strerror(errno));
        }
        err = pthread_cond_destroy(&(q->cond_r));
        if (err != 0) {
            fprintf(stderr, "queue cond var read destroy error: %s\n", strerror(errno));
        }
        err = pthread_cond_destroy(&(q->cond_w));
        if (err != 0) {
            fprintf(stderr, "queue cond var write destroy error: %s\n", strerror(errno));
        }
        abort();
	}

	return q;
}

void queue_destroy(queue_t *q) {
    int err;
    err = pthread_cancel(q->qmonitor_tid);
    if (err != 0) {
        fprintf(stderr, "qmonitor thread cancel error: %d\n", err);
    } else {
        err = pthread_join(q->qmonitor_tid, NULL);
        if (err != 0)
            fprintf(stderr, "qmonitor thread join error: %d\n", err);
    }
    while (q->first != NULL) {
        qnode_t *first = (qnode_t *) q->first;
        q->first = q->first->next;
        free(first);
    }
    err = pthread_mutex_destroy(&(q->lock));
    if (err != 0) {
        fprintf(stderr, "queue lock destroy error: %s\n", strerror(errno));
    }
    err = pthread_cond_destroy(&(q->cond_r));
    if (err != 0) {
        fprintf(stderr, "queue cond var read destroy error: %s\n", strerror(errno));
    }
    err = pthread_cond_destroy(&(q->cond_w));
    if (err != 0) {
        fprintf(stderr, "queue cond var write destroy error: %s\n", strerror(errno));
    }
    free(q);
}

int queue_add(queue_t *q, int val) {
    int err;
    if((err = pthread_mutex_lock(&(q->lock))) != 0) {
        fprintf(stderr, "pthread_mute_lock() error: %s\n", strerror(err));
        return 0;
    }
	q->add_attempts++;

	assert(q->count <= q->max_count);

	while (q->count == q->max_count) {
        if((err = pthread_cond_wait(&(q->cond_w), &(q->lock))) != 0) {
            fprintf(stderr, "pthread_cond_wait() error: %s\n", strerror(err));
            return 0;
        }
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
    pthread_cond_signal(&(q->cond_r));
    pthread_mutex_unlock(&(q->lock));
	return 1;
}

int queue_get(queue_t *q, int *val) {
    int err;
    if((err = pthread_mutex_lock(&(q->lock))) != 0) {
        fprintf(stderr, "pthread_mute_lock() error: %s\n", strerror(err));
        return 0;
    }
	q->get_attempts++;

	assert(q->count >= 0);

	while (q->count == 0) {
        if((err = pthread_cond_wait(&(q->cond_r), &(q->lock))) != 0) {
            fprintf(stderr, "pthread_cond_wait() error: %s\n", strerror(err));
            return 0;
        }
    }

	qnode_t *tmp = (qnode_t *) q->first;

	*val = tmp->val;
	q->first = q->first->next;

	free(tmp);
	q->count--;
	q->get_count++;

    pthread_cond_signal(&(q->cond_w));
    pthread_mutex_unlock(&(q->lock));
	return 1;
}

void queue_print_stats(queue_t *q) {
    if(pthread_mutex_lock(&(q->lock)) != 0) {
        return;
    }
	printf("queue stats: current size %d; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
		q->count,
		q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,
		q->add_count, q->get_count, q->add_count -q->get_count);
    pthread_mutex_unlock(&(q->lock));
}

