#include "threadList.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

struct ThreadInitiatorArgs {
    ThreadList *list;
    void *thread_data;

    void (*start)(void *args);
};

ThreadList *create_thread_list(void (*_cleanup_func)(void *)) {
    ThreadList *list = (ThreadList *) malloc(sizeof(ThreadList));
    if (list == NULL) {
        fputs("create_thread_list() can't allocate memory\n", stderr);
        abort();
    }
    list->first = NULL;
    list->cleanup_func = _cleanup_func;
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    // Always return 0 with argument PTHREAD_MUTEX_ERRORCHECK_NP
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK_NP);
    // Always return 0 with argument PTHREAD_PROCESS_PRIVATE
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_PRIVATE);
    // Always return 0
    pthread_mutex_init(&(list->mutex), &mutex_attr);
    // Always return 0
    pthread_mutexattr_destroy(&mutex_attr);
    return list;
}

static int busyMutexDestroy(pthread_mutex_t *mutex) {
    int err = 0;
    do {
        if (err == EBUSY)
            sched_yield();
        err = pthread_mutex_destroy(mutex);
    } while (err == EBUSY);
    return err;
}

int deconstruct_thread_list(ThreadList *list) {
    int err;
    ThreadNode *listNodes;
    err = pthread_mutex_lock(&list->mutex);
    if (err != 0)
        return err;
    listNodes = list->first;
    list->first = NULL;
    pthread_mutex_unlock(&list->mutex);
    while (listNodes != NULL) {
        debug("deconstruct_thread_list(): deconstruct  thread %p\n", listNodes)
        err = pthread_cancel(listNodes->thread);
        if (err != 0) {
            fprintf(stderr, "deconstruct_thread_list() error during pthread_cancel() %d\n", err);
            err = 0;
        } else {
            err = pthread_join(listNodes->thread, NULL);
        }
        if (err != 0)
            fprintf(stderr, "deconstruct_thread_list() error during pthread_join() %d\n", err);
        ThreadNode *next = listNodes->next;
        free(listNodes);
        listNodes = next;
    }
    busyMutexDestroy(&(list->mutex));
    free(list);
    return 0;
}

int remove_thread_from_list(ThreadList *list, pthread_t thread) {
    int err;
    ThreadNode *nodeToDelete = NULL;
    err = pthread_mutex_lock(&list->mutex);
    if (err != 0)
        return err;
    for (ThreadNode **i = &(list->first); *i != NULL; i = &((*i)->next)) {
        ThreadNode *node = *i;
        if (pthread_equal(thread, node->thread)) {
            (*i) = node->next;
            nodeToDelete = node;
            break;
        }
    }
    pthread_mutex_unlock(&list->mutex);
    if (nodeToDelete == NULL) {
        return ESRCH;
    }
    debug("remove_thread_from_list(): deconstruct  thread %p\n", nodeToDelete)
    pthread_t targetThread = nodeToDelete->thread;
    free(nodeToDelete);
    if (!pthread_equal(pthread_self(), targetThread)) {
        //Other thread stop this node, wait for terminate.
        pthread_cancel(targetThread);
        pthread_join(targetThread, NULL);
    }
    return 0;
}

void clean_self(ThreadList *arg) {
    ThreadList *list = (ThreadList *) arg;
    int err;
    err = remove_thread_from_list(list, pthread_self());
    // err == 0 thread terminate itself, detach this thread
    if (err == 0) {
        pthread_detach(pthread_self());
        return;
    }
    // err == ESRCH - thread already canceled, noting to do
    // err - other error - unknown state, delegate thread terminate for deconstruct_thread_list
    if (err != ESRCH) {
        fprintf(stderr, "Thread node stop error %s\n", strerror(err));
    }
}

void *thread_creator(void *a) {
    struct ThreadInitiatorArgs *args = (struct ThreadInitiatorArgs *) a;
    ThreadList *list = args->list;
    pthread_cleanup_push((void (*)(void *)) clean_self, list)
            pthread_cleanup_push(free, args)
                    pthread_cleanup_push(list->cleanup_func, args->thread_data)
                            args->start(args->thread_data);
                    pthread_cleanup_pop(1);
            pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
    return NULL;
}

int add_thread_to_list(ThreadList *list, void (*_start_routine)(void *args), void *threadData, pthread_t *thread) {
    int err;
    ThreadNode *node = (ThreadNode *) malloc(sizeof(ThreadNode));
    if (node == NULL) {
        fputs("add_thread_to_list() can't allocate memory\n", stderr);
        abort();
    }
    struct ThreadInitiatorArgs *creatorArgs = (struct ThreadInitiatorArgs *) malloc(sizeof(struct ThreadInitiatorArgs));
    if (creatorArgs == NULL) {
        fputs("add_thread_to_list() can't allocate memory\n", stderr);
        abort();
    }
    creatorArgs->list = list;
    creatorArgs->start = _start_routine;
    creatorArgs->thread_data = threadData;
    err = pthread_mutex_lock(&(list->mutex));
    if (err != 0)
        return err;
    node->next = list->first;
    list->first = node;
    err = pthread_create(&node->thread, NULL, thread_creator, creatorArgs);
    if (err != 0) {
        list->first = node->next;
        pthread_mutex_unlock(&(list->mutex));
        debug("Create thread node error %p\n", node)
        free(node);
        free(creatorArgs);
        return err;
    } else {
        pthread_mutex_unlock(&(list->mutex));
        if (thread != NULL)
            *thread = node->thread;
        debug("add_thread_to_list() %p\n", node)
        return 0;
    }
}
