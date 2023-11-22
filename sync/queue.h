#ifndef __FITOS_QUEUE_H__
#define __FITOS_QUEUE_H__

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

typedef struct _QueueNode {
	volatile int val;
	volatile struct _QueueNode *next;
} qnode_t;

typedef struct _Queue {
	volatile qnode_t *first;
	volatile qnode_t *last;

    pthread_mutex_t lock;
    pthread_cond_t cond_r;
    pthread_cond_t cond_w;
	pthread_t qmonitor_tid;

	volatile int count;
	volatile int max_count;

	// queue statistics
	volatile long add_attempts;
	volatile long get_attempts;
	volatile long add_count;
	volatile long get_count;
} queue_t;

queue_t* queue_init(int max_count);
void queue_destroy(queue_t *q);
int queue_add(queue_t *q, int val);
int queue_get(queue_t *q, int *val);
void queue_print_stats(queue_t *q);

#endif		// __FITOS_QUEUE_H__
