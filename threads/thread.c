#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

void *mythread(void *arg) {
	printf("mythread [%d %d %d]: Hello from mythread!\n", getpid(), getppid(), gettid());
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
        printf("main: create thread %ld\n", tid);
    }

    err = pthread_join(tid, NULL);
    if (err != 0) {
        fprintf(stderr, "main: pthread_join() failed: %s\n", strerror(err));
    } else {
        printf("main: join thread %ld\n", tid);
    }

	return 0;
}

