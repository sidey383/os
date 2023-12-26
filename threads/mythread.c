#define _GNU_SOURCE

#include "mythread.h"
#include <unistd.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <errno.h>
#include <sched.h>

#define THREAD_MEMORY_SIZE (4096*4)

#define THREAD_GUARD_SIZE 4096



typedef struct mythread_data_t mythread_data_t;

struct mythread_data_t {
    pid_t pid;
    sem_t sem_result;
    void* args;
};

int mythread_start(void* data) {

}

int mythread_create(mythread_t* thread, void *(start_routine), void *arg) {
    int status;
    int error;
    void* threadRegion;
    void* stackStart;
    mythread_data_t* data;
    threadRegion = mmap(NULL, THREAD_MEMORY_SIZE + THREAD_GUARD_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (threadRegion == MAP_FAILED) {
        return errno;
    }
    status = mprotect(threadRegion, THREAD_GUARD_SIZE, PROT_NONE);
    if (status != 0) {
        error = errno;
        //The result is ignored, nothing can be done in any situation
        munmap(threadRegion, THREAD_MEMORY_SIZE + THREAD_GUARD_SIZE);
        return error;
    }
    stackStart = threadRegion + THREAD_MEMORY_SIZE + THREAD_GUARD_SIZE - sizeof(mythread_data_t);
    data = stackStart;
    clone(mythread_start,
            stackStart,
          CLONE_THREAD,
            data,
            data);
    (*thread) = threadRegion;
    return 0;
}