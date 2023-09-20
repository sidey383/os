#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

const int N = 5;

int global;

void *mythread(void *arg) {
    int local = 0;
    static int localStatic = 0;
    const int localConst = 2;
    printf(
        "mythread [pid: %d ppid: %d tid: %d self: %ld] local %p local static %p local const %p global %p \n",
        getpid(), getppid(), gettid(),
        pthread_self(),
        &local, &localStatic, &localConst, &global
    );
    local++;
    global++;
    return (void*)0;
}

int main() {
    pthread_t tid[N];
    int err;

    printf("main [%d %d %d]: Hello from main!\n", getpid(), getppid(), gettid());
    for (int i = 0; i < N; i++) {
        err = pthread_create(&(tid[i]), NULL, mythread, NULL);
        if (err != 0) {
            fprintf(stderr, "main: pthread_create() failed: %s\n", strerror(err));
            return -1;
        } else {
            printf("Create thread tid: %ld\n", tid[i]);
        }
    }
    void* status_addr;
    for (int i = 0; i < N; i++) {
        err = pthread_join(tid[i], &status_addr);
        if (err != 0) {
            fprintf(stderr, "main: pthread_join() failed: %s\n", strerror(err));
        }
    }
    return 0;
}

