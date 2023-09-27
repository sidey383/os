#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct params {
    char* str;
    int num;
} params;

void *mythread(void *arg) {
    params* val = (params*) arg;
    printf("mythread [%d %d %d]: arg %d %s\n", getpid(), getppid(), gettid(), val->num, val->str);
    return NULL;
}

int main() {
    pthread_t tid;
    int err;
    params val;

    val.str = "hello world";
    val.num = 42;

    printf("main [%d %d %d]: Hello from main!\n", getpid(), getppid(), gettid());
    err = pthread_create(&tid, NULL, mythread, &val);
    if (err != 0) {
        fprintf(stderr, "main: pthread_create() failed: %s\n", strerror(err));
        return -1;
    } else {
        printf("Create thread tid: %ld\n", tid);
    }

    pthread_exit(NULL);
    return 0;
}

