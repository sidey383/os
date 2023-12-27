#define _GNU_SOURCE

#include "mythread.h"
#include <unistd.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#define THREAD_MEMORY_SIZE (4096*6)

#define THREAD_GUARD_SIZE 4096


typedef struct mythread_data_t mythread_data_t;

struct mythread_data_t {
    void *mem_start;
    size_t mem_size;
    pid_t pid;
    sem_t sem_terminate;
    sem_t sem_join;
    void *(*start_routine)(void *);
    void *arg;
    void *ret;
};

int mythread_start(mythread_data_t *data) {
    int status;
    data->ret = data->start_routine(data->arg);
    status = sem_post(&data->sem_terminate);
    if (status != 0) {
        fprintf(stderr, "mythread_start(): error while mark thread as terminated %s\n", strerror(errno));
        return 0;
    }
    return 0;
}

pid_t mythread_gettid(mythread_t thread) {
    return ((mythread_data_t*) thread)->pid;
}

int mythread_join(mythread_t thread, void **ret) {
    mythread_data_t* data = thread;
    int status;
    int error;
    status = sem_trywait(&data->sem_join);
    // thread already joined
    if (status != 0 && errno == EAGAIN)
        return EINVAL;
    if (status != 0)
        return errno;
    status = sem_wait(&data->sem_terminate);
    if (status != 0) {
        error = errno;
        status = sem_post(&data->sem_join);
        if (status != 0) {
            fprintf(stderr, "mythread_join(): release thread joining error: %s\n", strerror(errno));
        }
        return error;
    }
    (*ret) = data->ret;
    status = munmap(data->mem_start, data->mem_size);
    if (status != 0) {
        //Thread already terminate successful, just print error
        fprintf(stderr, "mythread_join(): munmap error: %s\n", strerror(errno));
    }
    return 0;
}

int mythread_create(mythread_t *thread, void *(*start_routine)(void *), void *arg) {
    int status;
    int error;
    void *threadRegion;
    void *stackStart;
    mythread_data_t *data;
    //Allocate memory
    threadRegion = mmap(NULL, THREAD_MEMORY_SIZE + THREAD_GUARD_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (threadRegion == MAP_FAILED) {
        return errno;
    }
    status = mprotect(threadRegion, THREAD_GUARD_SIZE, PROT_NONE);
    if (status != 0) {
        error = errno;
        status = munmap(threadRegion, THREAD_MEMORY_SIZE + THREAD_GUARD_SIZE);
        if (status != 0) {
            fprintf(stderr, "mythread_create(): munmap error: %s\n", strerror(errno));
        }
        return error;
    }
    stackStart = threadRegion + THREAD_MEMORY_SIZE + THREAD_GUARD_SIZE - sizeof(mythread_data_t);
    //Initialize data structure
    data = stackStart;
    data->start_routine = start_routine;
    data->arg = arg;
    data->mem_start = threadRegion;
    data->mem_size = THREAD_MEMORY_SIZE + THREAD_GUARD_SIZE;
    data->ret = NULL;

    //EINVAL value exceeds SEM_VALUE_MAX.
    //ENOSYS pshared is nonzero, but the system does not support process-shared semaphores
    //Impossible errors
    sem_init(&data->sem_join, 0, 1);
    sem_init(&data->sem_terminate, 0, 0);

    status = clone((int (*)(void *)) mythread_start,
                   stackStart,
                   CLONE_THREAD | CLONE_SIGHAND | CLONE_VM | CLONE_PARENT_SETTID,
                   data,
                   &data->pid);
    if (status == -1) {
        error = errno;
        //Error only when semaphore invalid, ignore it
        sem_destroy(&data->sem_join);
        sem_destroy(&data->sem_terminate);
        //The result is ignored, nothing can be done in any situation
        status = munmap(threadRegion, THREAD_MEMORY_SIZE + THREAD_GUARD_SIZE);
        if (status != 0) {
            fprintf(stderr, "mythread_create(): munmap error: %s\n", strerror(errno));
        }
        return errno;
    }
    (*thread) = data;
    return 0;
}