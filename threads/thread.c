#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

void freeData(void* arg) {
    printf("free %p\n", arg);
    free(arg);
}

void* mythread(void* arg) {
    char* str = (char*) malloc(100);
    printf("malloc %p\n", str);
    pthread_cleanup_push(freeData, str);
    strcpy(str, "hello world\n");
    int len = strlen(str);
    while (1) {
        write(STDOUT_FILENO, str, len);
    }
    pthread_cleanup_pop(1);
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
    }
    pthread_exit(NULL);
    return 0;
}

