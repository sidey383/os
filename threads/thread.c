#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

void *thread1(void *arg) {
    printf("thread1 [%d %d %d]: Hello from mythread!\n", getpid(), getppid(), gettid());
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGUSR1);
    err = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (err != 0) {
        fprintf(stderr, "main: pthread_sigmask() failed: %s\n", strerror(err));
        return -1;
    }
    return NULL;
}

void *thread2(void* arg) {
    printf("thread2 [%d %d %d]: Hello from mythread!\n", getpid(), getppid(), gettid());
    return NULL;
}

void *thread3(void* arg) {
    printf("thread3 [%d %d %d]: Hello from mythread!\n", getpid(), getppid(), gettid());
    return NULL;
}


int main() {
    pthread_t tid;
    int err;
    printf("main [%d %d %d]: Hello from main!\n", getpid(), getppid(), gettid());
    err = pthread_create(&tid, NULL, thread1, NULL);
    if (err != 0) {
        fprintf(stderr, "main: pthread_create() failed: %s\n", strerror(err));
        return -1;
    } else {
        printf("Create thread tid: %ld\n", tid);
    }

    pthread_exit(NULL);
    return 0;
}

