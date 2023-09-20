#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#define BUF_SIZE 8192

void printProcMap(void) {
    char *path = "/proc/self/maps";
    int file = -1;
    if ((file = open(path, O_NOFOLLOW, S_IREAD)) < 0) {
        perror("File open error");
    }
    char buf[BUF_SIZE];
    ssize_t readN;
    while ((readN = read(file, buf, BUF_SIZE)) > 0) {
        if (errno < 0) {
            perror("Read error");
            break;
        }
        int writeN = 0;
        while (writeN < readN) {
            int w;
            w = write(STDOUT_FILENO, buf + writeN, readN - writeN);
            if (w < 0) {
                perror("Write error");
                break;
            }
            writeN += w;
        }
    }
    close(file);
    write(STDOUT_FILENO, "\n", 1);
}

const int N = 5;

int global;

void *mythread(void *arg) {
    int local = 0;
    static int localStatic = 0;
    const int localConst = 2;
    printf(
        "mythread [pid: %d ppid: %d tid: %d self: %ld] local %p:%d local static %p:%d local const %p:%d global %p:%d \n",
        getpid(), getppid(), gettid(),
        pthread_self(),
        &local, local, &localStatic, localStatic, &localConst, localConst, &global, global
    );
    local++;
    global++;
    return (void*)0;
}

int main() {
    pthread_t tid[N];
    int err;

    printProcMap();

    printf("main [%d %d %d]: Hello from main!\n", getpid(), getppid(), gettid());
    for (int i = 0; i < N; i++) {
        err = pthread_create(&(tid[i]), NULL, mythread, NULL);
        if (err != 0) {
            fprintf(stderr, "main: pthread_create() failed: %s\n", strerror(err));
            return -1;
        } else {
            printf("Create thread tid: %ld\n", tid[i]);
        }
        printProcMap();
    }
    void* status_addr;
    for (int i = 0; i < N; i++) {
        err = pthread_join(tid[i], &status_addr);
        if (err != 0) {
            fprintf(stderr, "main: pthread_join() failed: %s\n", strerror(err));
        } else {
            printf("main: pthread_join(): %ld\n", tid[i]);
        }
        printProcMap();
    }
    printProcMap();
    return 0;
}

