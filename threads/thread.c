#define _GNU_SOURCE

#include <stdio.h>
#include "mythread.h"
#include <string.h>
#include <unistd.h>

void *mythread(void *arg) {
    sleep((int)(long)arg);
    printf("[%d %d %d]: Hello from mythread!\n", getpid(), getppid(), gettid());
    return (void *)(long)gettid();
}

int main() {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    setbuf(stdin, NULL);
    mythread_t tid[10];
    int err;

    printf("[%d %d %d]: Hello from main!\n", getpid(), getppid(), gettid());

    for (int i = 0; i < 10; i++) {
        err = mythread_create(&tid[i], mythread, (void*)(long)(i/2));
        if (err) {
            fprintf(stderr, "[%d %d %d]: mythread_create() failed: %s\n", getpid(), getppid(), gettid(), strerror(err));
            return -1;
        } else {
            printf("[%d %d %d]: Create thread %d tid %d\n", getpid(), getppid(), gettid(), i, mythread_gettid(tid[i]));
        }
    }
    void* ret;
    for (int i = 9; i >= 0; i--) {
        err = mythread_join(tid[i], &ret);
        if (err) {
            fprintf(stderr, "[%d %d %d]: mythread_create() failed: %s\n", getpid(), getppid(), gettid(), strerror(err));
            return -1;
        } else {
            printf("[%d %d %d]: Thread %d return %d\n", getpid(), getppid(), gettid(), i, (int)(long)ret);
        }
    }

    return 0;
}

