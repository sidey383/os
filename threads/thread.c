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
    // int pthread_attr_init(pthread_attr_t *attr); - Создать атрибуты потока
    // int pthread_attr_destroy(pthread_attr_t *attr); - Уничтожить артибуты потока
    // 1)
    // int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate); - установить состояние detached
    //   PTHREAD_CREATE_DETACHED - созданный поток будет detached (нельзя просоеденить через join)
    //   PTHREAD_CREATE_JOINABLE - создает присоеднияемый поток (по умолчанию)
    // 2)
    // int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize); - уставить размезмер защищаемой зона
    //  после стека будет защищаенная зона размера не меньше guardsize
    //  по умолчанию равен размера страницы
    // 3)
    // int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched); - тип наследования планировщика
    //  PTHREAD_INHERIT_SCHED - наследовать планировщик из текущего потока
    //  PTHREAD_EXPLICIT_SCHED - использовать планировщик заданный в атрибутах
    // 4)
    // int pthread_attr_setschedparam(pthread_attr_t *attr, const struct sched_param *param); - установить параметры планировщика
    //   Используемая структура:
    //   struct sched_param {
    //      int sched_priority;     Приоритет планирования, от -20 (самый высокй приоритет) до  19 (самый низкий приоритет)
    //   };
    //   Приоритет по умолчанию 0
    // 5)
    // int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy); - устанавливает политику планировщика
    // 6)
    // int pthread_attr_setscope(pthread_attr_t *attr, int scope); - определяет потоки, с котороыми созданный поток будет конкурировать за ресурсы
    // PTHREAD_SCOPE_SYSTEM - конкурирует со всеми потоками в системе
    // PTHREAD_SCOPE_PROCESS - конкурирует за ресурсы только с потоками созданными этим процессов с такой же областью видимости
    // неопределено как эти потоки взаимодействую с потоками других процессов или с потоками другой области видимости.
    // 7)
    // int pthread_attr_setstack(pthread_attr_t *attr, void *stackaddr, size_t stacksize);
    // Определяет стек, который будет использовать новый поток.
    // stackaddr - указывает на младший адресуемый байт буфера для стэка
    // stacksize - размер стэка
    // 8)
    // int pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr);
    // см 7, устанавливает только адрес стэка
    // 9)
    // int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
    // см 7, устанавливает только размер стэка
    err = pthread_create(&tid, NULL, mythread, NULL);
    // int pthread_getattr_np(pthread_t thread, pthread_attr_t *attr);
    // Возвращает фактические данные о исполняемом потоке
    if (err != 0) {
        fprintf(stderr, "main: pthread_create() failed: %s\n", strerror(err));
        return -1;
    } else {
        printf("Create thread tid: %ld\n", tid);
    }

    pthread_exit(NULL);
    return 0;
}

