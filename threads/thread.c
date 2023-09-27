#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

int counter = 0;

void *mythread(void *arg) {
    while (1) {
        counter++;
    }
    return NULL;
}

int main() {
    pthread_t tid;
    int err;

    printf("main [%d %d %d]: Hello from main!\n", getpid(), getppid(), gettid());
    err = pthread_create(&tid, NULL, mythread, NULL);
    if (err != 0) {
        fprintf(stderr, "main: pthread_create() failed: %s\n", strerror(err));
        return -1;
    } else {
        printf("Create thread tid: %ld\n", tid);
    }
    err = pthread_cancel(tid);
    if (err != 0) {
        fprintf(stderr, "main: pthread_cancel() failed: %s\n", strerror(err));
        return -1;
    } else {
        printf("counter: %d\n", counter);
    }
    pthread_exit(NULL);
    return 0;
}

