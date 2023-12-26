#define _GNU_SOURCE

#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>

#include "list.h"
#include "stdio.h"

Storage *create_storage() {
    int err;
    Storage *st = malloc(sizeof(Storage));
    if (st == NULL)
        return NULL;
    st->first.next = NULL;
    err = pthread_spin_init(&st->first.lock, PTHREAD_PROCESS_PRIVATE);
    if (err != 0) {
        fprintf(stderr, "Can't create spinlock: %s\n", strerror(err));
        free(st);
        return NULL;
    }
    return st;
}

size_t strlenVal(const char *s){
    char *z = memchr(s, 0, STR_SIZE);
    return z ? z-s : STR_SIZE;
}

int add_storage_value(Storage* storage, const char* val) {
    int err;
    err = pthread_spin_lock(&storage->first.lock);
    if (err != 0)
        return err;
    Node* node = malloc(sizeof(Node));
    if (node == NULL)
        return ENOMEM;
    memcpy(node->value, val, strlenVal(val));
    err = pthread_spin_init(&node->core.lock, PTHREAD_PROCESS_PRIVATE);
    if (err != 0) {
        free(node);
        return err;
    }
    node->core.next = storage->first.next;
    storage->first.next = node;
    err = pthread_spin_unlock(&storage->first.lock);
    if (err != 0)
        return err;
    return 0;
}

void free_storage(Storage* storage) {
    pthread_spin_destroy(&storage->first.lock);
    Node* i = storage->first.next;
    while (i != NULL) {
        pthread_spin_destroy(&i->core.lock);
        Node* tmp = i;
        i = i->core.next;
        free(tmp);
    }
    free(storage);
}

int start_iterator(StorageIterator *iterator, Storage *storage) {
    int error;
    error = pthread_spin_lock(&storage->first.lock);
    if (error != 0)
        return error;
    iterator->current_node = storage->first.next;
    if (iterator->current_node == NULL) {
        pthread_spin_unlock(&storage->first.lock);
        return 0;
    }
    error = pthread_spin_lock(&iterator->current_node->core.lock);
    pthread_spin_unlock(&storage->first.lock);
    if (error != 0)
        return error;
    return 0;
}

int stop_iterator(StorageIterator *iterator) {
    int error;
    if (iterator->current_node == NULL)
        return 0;
    error = pthread_spin_unlock(&iterator->current_node->core.lock);
    if (error != 0)
        return error;
    iterator->current_node = 0;
    return 0;
}

GetStatus get_value(StorageIterator *iterator, char **val) {
    if (iterator->current_node == NULL)
        return GET_NO_VALUE;
    (*val) = iterator->current_node->value;
    return GET_OK;
}

NextStatus next_iterator(StorageIterator *iterator) {
    int error;
    if (iterator->current_node == NULL)
        return NEXT_END;
    NodeCore *core = &iterator->current_node->core;
    Node *next = core->next;
    if (next != NULL) {
        error = pthread_spin_lock(&next->core.lock);
        if (error != 0)
            return NEXT_LOCK_ERROR;
    }
    iterator->current_node = next;
    error = pthread_spin_unlock(&core->lock);
    if (error != 0)
        return NEXT_UNLOCK_ERROR;
    return NEXT_OK;
}

int start_active_iterator(StorageActiveIterator *iterator, Storage *storage) {
    int error;
    iterator->core = &storage->first;
    error = pthread_spin_lock(&iterator->core->lock);
    if (error != 0)
        return error;
    iterator->first_node = iterator->core->next;
    if (iterator->first_node != NULL) {
        error = pthread_spin_lock(&iterator->first_node->core.lock);
        if (error != 0) {
            pthread_spin_unlock(&iterator->core->lock);
            return error;
        }
        iterator->second_node = iterator->first_node->core.next;
    } else {
        iterator->second_node = NULL;
    }

    if (iterator->second_node != NULL) {
        error = pthread_spin_lock(&iterator->second_node->core.lock);
        if (error != 0) {
            pthread_spin_unlock(&iterator->core->lock);
            pthread_spin_unlock(&iterator->first_node->core.lock);
            return error;
        }
    }
    return 0;
}

int stop_active_iterator(StorageActiveIterator *iterator) {
    int error;
    if (iterator->core != NULL) {
        error = pthread_spin_unlock(&iterator->core->lock);
        if (error != 0)
            return error;
    }
    if (iterator->first_node != NULL) {
        error = pthread_spin_unlock(&iterator->first_node->core.lock);
        if (error != 0)
            return error;
    }
    if (iterator->second_node != NULL) {
        error = pthread_spin_unlock(&iterator->second_node->core.lock);
        if (error != 0)
            return error;
    }
    return 0;
}

NextStatus next_active_iterator(StorageActiveIterator *iterator) {
    int error;
    Node *next = NULL;
    NodeCore *core = iterator->core;
    // check iterator on end
    if (iterator->first_node == NULL) {
        if (iterator->core != NULL) {
            error = pthread_spin_unlock(&iterator->core->lock);
            if (error != 0)
                return NEXT_UNLOCK_ERROR;
            iterator->core = NULL;
        }
        return NEXT_END;
    }

    //Select and lock next node
    if (iterator->second_node != NULL) {
        next = iterator->second_node->core.next;
        if (next != NULL) {
            error = pthread_spin_lock(&next->core.lock);
            if (error != 0) {
                fprintf(stderr, "next_active_iterator() lock error: %s\n", strerror(error));
                return NEXT_LOCK_ERROR;
            }
        }
    }

    iterator->core = &iterator->first_node->core;
    iterator->first_node = iterator->second_node;
    iterator->second_node = next;
    error = pthread_spin_unlock(&core->lock);
    if (error != 0)
        return NEXT_UNLOCK_ERROR;
    //Reach end, free core
    if (iterator->first_node == NULL) {
        if (iterator->core != NULL) {
            error = pthread_spin_unlock(&iterator->core->lock);
            if (error != 0)
                return NEXT_UNLOCK_ERROR;
            iterator->core = NULL;
        }
    }
    return 0;
}

GetStatus get_active_first_value(StorageActiveIterator* iterator, char** val) {
    if (iterator->first_node == NULL)
        return GET_NO_VALUE;
    (*val) = iterator->first_node->value;
    return GET_OK;
}

GetStatus get_active_second_value(StorageActiveIterator* iterator, char** val) {
    if (iterator->second_node == NULL)
        return GET_NO_VALUE;
    (*val) = iterator->second_node->value;
    return GET_OK;
}

SwapStatus swap_active_iterator(StorageActiveIterator* iterator) {
    if (iterator->core == NULL)
        return SWAP_NO_NODE;
    if (iterator->first_node == NULL)
        return SWAP_NO_NODE;
    if (iterator->second_node == NULL)
        return SWAP_NO_NODE;
    iterator->core->next = iterator->second_node;
    iterator->first_node->core.next = iterator->second_node->core.next;
    iterator->second_node->core.next = iterator->first_node;
    iterator->first_node = iterator->core->next;
    iterator->second_node = iterator->first_node->core.next;
    return SWAP_OK;
}