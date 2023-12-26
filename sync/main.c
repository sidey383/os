#define _GNU_SOURCE
#include <unistd.h>
#include <sched.h>
#include "list.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

volatile int eq_iteration_count;
volatile int gr_iteration_count;
volatile int lw_iteration_count;
volatile int mod_count[3];
volatile int mod_cycle[3];

volatile int eq_iteration_state;
volatile int gr_iteration_state;
volatile int lw_iteration_state;

struct CmpArgs {
    Storage *storage;
    int (*cmpFunc)(size_t last, size_t cur);
    volatile int *counter;
    volatile int *state;
    int cpu;
};

struct ModArgs {
    Storage *storage;
    volatile int *counter;
    volatile int *cycle;
    unsigned int seed;
    int cpu;
};

void set_cpu(int n) {
    int err;
    cpu_set_t cpuset;
    pthread_t tid = pthread_self();

    CPU_ZERO(&cpuset);
    CPU_SET(n, &cpuset);

    err = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpuset);
    if (err) {
        printf("set_cpu: pthread_setaffinity failed for cpu %d\n", n);
        abort();
    }
    printf("set_cpu: set cpu %d\n", n);
}

int equal(size_t last, size_t cur) {
    return last == cur;
}

int great(size_t last, size_t cur) {
    return last < cur;
}

int lower(size_t last, size_t cur) {
    return last > cur;
}

void fill_storage(Storage *storage, int count) {
    int err;
    char buf[STR_SIZE];
    unsigned int seed = 0;
    for (int i = 0; i < STR_SIZE; i++) {
        buf[i] = 'a';
    }
    for (int i = 0; i < count; i++) {
        int size = rand_r(&seed) % STR_SIZE;
        buf[size] = 0;
        err = add_storage_value(storage, buf);
        if (err != 0) {
            fprintf(stderr, "Value fill error: %s\n", strerror(err));
            abort();
        }
        buf[size] = 'a';
    }
}

int cmp_thread(struct CmpArgs *args) {
    set_cpu(args->cpu);
    Storage *storage = args->storage;
    int (*cmpFunc)(size_t last, size_t cur) = args->cmpFunc;
    volatile int *counter = args->counter;
    volatile int *state = args->state;
    int error;
    StorageIterator iterator;
    while (1) {
        start_iterator(&iterator, storage);
        size_t lastSize = -1;
        char *str;
        size_t size;
        NextStatus status;
        int count = 0;
        int isEnd = 1;
        while (isEnd) {
            get_value(&iterator, &str);
            size = strlen(str);
            if (lastSize != -1 && cmpFunc(lastSize, size))
                count++;
            lastSize = size;
            status = next_iterator(&iterator);
            switch (status) {
                case NEXT_OK:
                    break;
                case NEXT_END:
                    isEnd = 0;
                    break;
                case NEXT_LOCK_ERROR:
                    fprintf(stderr, "Next lock error\n");
                    abort();
                case NEXT_UNLOCK_ERROR:
                    fprintf(stderr, "Next unlock error\n");
                    abort();
            }
        }
        (*counter)++;
        (*state) = count;
        error = stop_iterator(&iterator);
        if (error != 0) {
            fprintf(stderr, "Stop iterator error: %s\n", strerror(error));
            abort();
        }
    }
}

int mod_thread(struct ModArgs *args) {
    set_cpu(args->cpu);
    Storage *storage = args->storage;
    volatile int *counter = args->counter;
    volatile int* cycle = args->cycle;
    unsigned int seed = args->seed;
    NextStatus status;
    int error;
    while (1) {
        StorageActiveIterator iterator;
        start_active_iterator(&iterator, storage);
        int isEnd = 1;
        while (isEnd) {
            if (rand_r(&seed) % 2 == 0) {
                swap_active_iterator(&iterator);
                (*counter)++;
            }
            status = next_active_iterator(&iterator);
            switch (status) {
                case NEXT_OK:
                    break;
                case NEXT_END:
                    isEnd = 0;
                    break;
                case NEXT_LOCK_ERROR:
                    fprintf(stderr, "Next active lock error\n");
                    abort();
                case NEXT_UNLOCK_ERROR:
                    fprintf(stderr, "Next active unlock error\n");
                    abort();
            }
        }
        (*cycle)++;
        error = stop_active_iterator(&iterator);
        if (error != 0) {
            fprintf(stderr, "Stop active iterator error: %s\n", strerror(error));
            abort();
        }
    }
}

_Noreturn void *monitor(void *arg) {
    set_cpu((int)(long)arg);
    int n = 0;
    while (1) {
        sleep(1);
        n++;
        printf("%03d EQ %010d GR %010d LW %010d Mod %010d Mod cycle %010d\n",
               n,
               eq_iteration_count,
               gr_iteration_count,
               lw_iteration_count,
               mod_count[0] + mod_count[1] + mod_count[2],
               mod_cycle[0] + mod_cycle[1] + mod_cycle[2]
        );
    }
}

int main() {
    int err;
    Storage *storage;
    pthread_t t[7];
    storage = create_storage();
    if (storage == NULL) {
        fprintf(stderr, "Storage create error\n");
        abort();
    }
    fill_storage(storage, 1000);
    struct CmpArgs cmpArgs[3] = {
        {storage, equal, &eq_iteration_count, &eq_iteration_state, 1},
        {storage, great, &gr_iteration_count, &gr_iteration_state, 2},
        {storage, lower, &lw_iteration_count, &lw_iteration_state, 3}
    };
    struct ModArgs modArgs[3] = {
            {storage, mod_count, mod_cycle, 222, 4},
            {storage, mod_count + 1, mod_cycle + 1, 333, 5},
            {storage, mod_count + 2, mod_cycle + 2, 444, 6}
    };
    for (int i = 0; i < 3; i++) {
        err = pthread_create(t+i, NULL, (void *(*)(void *)) cmp_thread, cmpArgs+i);
        if (err != 0) {
            fprintf(stderr, "Thread create error %s\n", strerror(err));
            abort();
        }
        err = pthread_create(t+3+i, NULL, (void *(*)(void *)) mod_thread, modArgs+i);
        if (err != 0) {
            fprintf(stderr, "Thread create error %s\n", strerror(err));
            abort();
        }
    }
    err = pthread_create(t+6, NULL, monitor, (void*)(long)7);
    if (err != 0) {
        fprintf(stderr, "Thread create error %s\n", strerror(err));
        abort();
    }
    pthread_exit(NULL);
    //Created thread never terminate. join will do noting, ignore this
}
