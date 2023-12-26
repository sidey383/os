#pragma once

typedef void* mythread_t;

int mythread_create(mythread_t* thread, void *(start_routine), void *arg);
