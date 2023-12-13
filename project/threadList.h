#pragma once
#include <pthread.h>

typedef struct ThreadNode ThreadNode;

typedef struct ThreadList ThreadList;

struct ThreadNode {
    pthread_t thread;
    ThreadNode* next;
};

struct ThreadList {
    ThreadNode* first;
    pthread_mutex_t mutex;
    void (*cleanup_func)(void*);
};

ThreadList* create_thread_list(void (*_cleanup_func)(void*));

int add_thread_to_list(ThreadList* list, void (*_start_routine) (void *args), void* thread_data, pthread_t* thread);

int remove_thread_from_list(ThreadList* list, pthread_t thread);

int deconstruct_thread_list(ThreadList* list);
