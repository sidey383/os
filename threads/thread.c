#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

void *mythread(void *arg) {
	printf("mythread [%d %d %d]: Hello from mythread!\n", getpid(), getppid(), gettid());
	pid_t* ret = malloc(sizeof(pid_t));
    (*ret) = gettid();
    return ret;
}

int main() {
	pthread_t tid;
	int err;

	printf("main [%d %d %d]: Hello from main!\n", getpid(), getppid(), gettid());
    while (1) {
	    err = pthread_create(&tid, NULL, mythread, NULL);
	    if (err != 0) {
	        fprintf(stderr, "main: pthread_create() failed: %s\n", strerror(err));
		    return -1;
	    } else {
            printf("main: create thread %ld\n", tid);
        }

        pid_t* val;
        err = pthread_join(tid, (void**)&val);
        if (err != 0) {
            fprintf(stderr, "main: pthread_join() failed: %s\n", strerror(err));
        } else {
            printf("main: join thread %ld return %d\n", tid, *val);
            free(val);
        }
    }

	return 0;
}

