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
        fprintf(stderr, "thread [%d %d %d]: %s error %s\n", getpid(), getppid(), gettid(), #f, strerror(err));\
        exit(-1);\
    }                          \
}

void *thread2() {

    sigset_t sigm;

    int err;

    Handle(err, sigfillset, &sigm)

    Handle(err, sigdelset, &sigm, SIGQUIT)

    Handle(err, pthread_sigmask, SIG_BLOCK, &sigm, NULL)

    Handle(err, sigemptyset, &sigm)

    Handle(err, sigaddset, &sigm, SIGQUIT)

    int sig;

    printf("[%d %d %d]: I catch SIGQUIT!\n", getpid(), getppid(), gettid());

    Handle(err, sigwait, &sigm, &sig)

    printf("[%d %d %d]: I've recive signal %s\n", getpid(), getppid(), gettid(), sigdescr_np(sig));
    printf("[%d %d %d]: I've complete!\n", getpid(), getppid(), gettid());
    return NULL;
}


void sigAction(int sig, siginfo_t* info, void* ucontext) {
    char buf[4094];
    int size = snprintf(buf, 4096, "[%d %d %d] sigaction handle %s from %d\n", getpid(), getppid(), gettid(), sigdescr_np(sig), info->si_pid);
    if (size > 0) {
        write(STDOUT_FILENO,buf, size);
    }
    pthread_exit(NULL);
}

void *thread1() {

    int err;

    sigset_t sigset;

    Handle(err, sigfillset, &sigset)

    Handle(err, sigdelset, &sigset, SIGINT)

    Handle(err, pthread_sigmask, SIG_BLOCK, &sigset, NULL)

    struct sigaction act;

    act.sa_sigaction = sigAction;

    act.sa_flags = SA_SIGINFO;

    Handle(err, sigaction, SIGINT, &act, NULL)

    printf("[%d %d %d]: I catch SIGINT!\n", getpid(), getppid(), gettid());

    while (1) {
        sleep(1);
    }

    printf("[%d %d %d]: I've finished!\n", getpid(), getppid(), gettid());

    return NULL;
}


int main() {
    pthread_t tid;
    int err;

    Handle(err, pthread_create, &tid, NULL, thread1, NULL)

    Handle(err, pthread_create, &tid, NULL, thread2, NULL)

    sigset_t sigset;

    Handle(err, sigfillset, &sigset)

    Handle(err, pthread_sigmask, SIG_BLOCK, &sigset, NULL)

    printf("[%d %d %d]: I don't catch signals!\n", getpid(), getppid(), gettid());

    sleep(10);

    printf("[%d %d %d]: I've finished!\n", getpid(), getppid(), gettid());

    pthread_exit(NULL);
}

