#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#define Handle(err, f, ...) {\
    err = f(__VA_ARGS__);              \
    if (err != 0) {             \
        printf("thread [%d %d %d]: %s error %s\n", getpid(), getppid(), gettid(), #f, strerror(err));\
        exit(-1);\
    }                          \
}

void *thread2() {

    sigset_t sigm;

    int err;

    Handle(err, sigfillset, &sigm)

    Handle(err, sigdelset, &sigm, SIGINT)

    Handle(err, pthread_sigmask, SIG_BLOCK, &sigm, NULL)

    Handle(err, sigemptyset, &sigm)

    Handle(err, sigaddset, &sigm, SIGINT)

    int sig;

    printf("thread [%d %d %d]: I catch SIGINT!\n", getpid(), getppid(), gettid());

    Handle(err, sigwait, &sigm, &sig)

    printf("thread [%d %d %d]: I recive signal %s\n", getpid(), getppid(), gettid(), sigdescr_np(sig));
    return NULL;
}


void sigHandler(int sig) {
    printf("thread [%d %d %d] SigintHandler handle %s\n", getpid(), getppid(), gettid(), sigdescr_np(sig));
    pthread_exit(NULL);
}

void *thread1() {

    int err;

    sigset_t sigset;

    Handle(err, sigfillset, &sigset)

    Handle(err, sigdelset, &sigset, SIGINT)

    Handle(err, pthread_sigmask, SIG_BLOCK, &sigset, NULL)

    struct sigaction act;

    act.sa_handler = sigHandler;

    Handle(err, sigaction, SIGINT, &act, NULL)

    printf("thread [%d %d %d]: I catch SIGINT!\n", getpid(), getppid(), gettid());

    sleep(60);
    return NULL;
}


int main() {
    pthread_t tid;
    int err;

    Handle(err, pthread_create, &tid, NULL, thread2, NULL)

    Handle(err, pthread_create, &tid, NULL, thread1, NULL)

    Handle(err, pthread_create, &tid, NULL, thread1, NULL)

    sigset_t sigset;

    Handle(err, sigfillset, &sigset)

    Handle(err, pthread_sigmask, SIG_BLOCK, &sigset, NULL)

    printf("thread [%d %d %d]: I don't catch signals!\n", getpid(), getppid(), gettid());

    //sleep(30);

    pthread_exit(NULL);
}

