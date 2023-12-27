#pragma once
#include <unistd.h>

typedef void* mythread_t;

int mythread_create(mythread_t* thread, void *(*start_routine)(void *), void *arg);

int mythread_join(mythread_t thread, void **ret);

pid_t mythread_gettid(mythread_t thread);
