#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

const int N = 5;

void *mythread(void *arg) {
    printf("mythread [%d %d %d]: Hello from mythread!\n", getpid(), getppid(), gettid());
    return NULL;
}

int main() {
    pthread_t tid;
    int err;

    printf("main [%d %d %d]: Hello from main!\n", getpid(), getppid(), gettid());
    for (int i = 0; i < N; i++) {
        // int pthread_attr_init(pthread_attr_t *attr); - Создать атрибуты потока
        // int pthread_attr_destroy(pthread_attr_t *attr); - Уничтожить артибуты потока
        // int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate); - установить состояние detached
        //   PTHREAD_CREATE_DETACHED - созданный поток будет detached (нельзя просоеденить через join)
        //   PTHREAD_CREATE_JOINABLE - создает присоеднияемый поток (по умолчанию)
        // int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize); - уставить размезмер защищаемой зона
        //  после стека будет защищаенная зона размера не меньше guardsize
        //  по умолчанию равен размера страницы
        // int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched); - тип наследования планировщика
        //  PTHREAD_INHERIT_SCHED - наследовать планировщик из текущего потока
        //  PTHREAD_EXPLICIT_SCHED - использовать планировщик заданный в атрибутах
        // int pthread_attr_setschedparam(pthread_attr_t *attr, const struct sched_param *param); - установить параметры планировщика
        //   Используемая структура:
        //   struct sched_param {
        //      int sched_priority;     Приоритет планирования, от -20 (самый высокй приоритет) до  19 (самый низкий приоритет)
        //   };
        //   Приоритет по умолчанию 0
        // int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy); - устанавливает политику планировщика
        //  
        err = pthread_create(&tid, NULL, mythread, NULL);
        if (err != 0) {
            fprintf(stderr, "main: pthread_create() failed: %s\n", strerror(err));
            return -1;
        } else {
            printf("Create thread tid: %ld\n", tid);
        }
        sleep(1);
    }

    pthread_exit(NULL);
    return 0;
}

