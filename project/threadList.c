#include "threadList.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

struct ThreadInitiatorArgs {
    ThreadList *list;
    void* thread_data;
    void (*start) (void* args);
};

ThreadList* create_thread_list(void (*_cleanup_func)(void*)) {
    ThreadList* list = (ThreadList*) malloc(sizeof(ThreadList));
    if (list == NULL) {
        fputs("create_thread_list() can't allocate memory", stderr);
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

int deconstruct_thread_list(ThreadList* list) {
    int err;
    ThreadNode *listNodes;
    err = pthread_mutex_lock(&list->mutex);
    if (err != 0)
        return err;
    listNodes = list->first;
    list->first = NULL;
    pthread_mutex_unlock(&list->mutex);
    while (listNodes != NULL) {
        err = pthread_cancel(listNodes->thread);
        if (err != 0) {
            fprintf(stderr, "deconstruct_thread_list() error during pthread_cancel() %d", err);
            err = 0;
        } else {
            err = pthread_join(listNodes->thread, NULL);
        }
        if (err != 0)
            fprintf(stderr, "deconstruct_thread_list() error during pthread_join() %d", err);
        ThreadNode *next = listNodes->next;
        free(listNodes);
        listNodes = next;
    }
    busyMutexDestroy(&(list->mutex));
    free(list);
    return 0;
}

int remove_thread_from_list(ThreadList* list, pthread_t thread) {
    int err;
    ThreadNode* nodeToDelete = NULL;
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
    if (pthread_equal(pthread_self(), nodeToDelete->thread)) {
        //The thread remove itself, nothing to wait.
        free(nodeToDelete);
        pthread_detach(pthread_self());
        pthread_exit(NULL);
    } else {
        //Other thread stop this node, wait for terminate.
        pthread_cancel(nodeToDelete->thread);
        pthread_join(nodeToDelete->thread, NULL);
        free(nodeToDelete);
    }
    return 0;
}

static void clean_self(void* arg) {
    ThreadList* list = (ThreadList*)arg;
    int err;
    err = remove_thread_from_list(list, pthread_self());
    if (err != ESRCH) {
        fprintf(stderr, "Thread node stop error %s", strerror(err));
    }
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

void* thread_creator(void* a) {
    struct ThreadInitiatorArgs* args = (struct ThreadInitiatorArgs*) a;
    pthread_cleanup_push(clean_self, args->list)
            pthread_cleanup_push(free, args)
                    pthread_cleanup_push(args->list->cleanup_func, args->thread_data)
                        args->start(args->thread_data);
                    pthread_cleanup_pop(1);
            pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
    // Unreachable
    return NULL;
}

int add_thread_to_list(ThreadList* list, void (*_start_routine) (void *args), void *threadData, pthread_t* thread) {
    int err;
    ThreadNode* node = (ThreadNode*) malloc(sizeof(ThreadNode));
    if (node == NULL) {
        fputs("add_thread_to_list() can't allocate memory", stderr);
        abort();
    }
    struct ThreadInitiatorArgs* creatorArgs = (struct ThreadInitiatorArgs*) malloc(sizeof(struct ThreadInitiatorArgs));
    if (creatorArgs == NULL) {
        fputs("add_thread_to_list() can't allocate memory", stderr);
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
        free(node);
        free(creatorArgs);
        return err;
    } else {
        pthread_mutex_unlock(&(list->mutex));
        if (thread != NULL)
            *thread = node->thread;
        return 0;
    }
}
