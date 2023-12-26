#pragma once
#include "pthread.h"

#define STR_SIZE 100

typedef struct _Node Node;

typedef struct _Storage Storage;

typedef struct _NodeCore NodeCore;

typedef struct _StorageIterator StorageIterator;

typedef struct _StorageActiveIterator StorageActiveIterator;

typedef enum _GetStatus GetStatus;

typedef enum _NextStatus NextStatus;

typedef enum _SwapStatus SwapStatus;

struct _NodeCore {
    Node *next;
    pthread_mutex_t lock;
};

struct _Node {
    char value[STR_SIZE];
    NodeCore core;
};

struct _Storage {
    NodeCore first;
};

struct _StorageIterator {
    Node *current_node;
};

struct _StorageActiveIterator {
    NodeCore *core;
    Node *first_node;
    Node *second_node;
};

enum _GetStatus {
    GET_OK = 0,
    GET_NO_VALUE = 1
};

enum _NextStatus {
    NEXT_OK = 0,
    NEXT_END = 1,
    NEXT_LOCK_ERROR = 2,
    NEXT_UNLOCK_ERROR = 3
};

enum _SwapStatus {
    SWAP_OK = 0,
    SWAP_NO_NODE = 1
};

Storage* create_storage();

void free_storage(Storage*);

int add_storage_value(Storage*, const char*);

int start_iterator(StorageIterator*, Storage*);

int stop_iterator(StorageIterator*);

GetStatus get_value(StorageIterator*, char**);

NextStatus next_iterator(StorageIterator*);

int start_active_iterator(StorageActiveIterator*, Storage*);

int stop_active_iterator(StorageActiveIterator*);

NextStatus next_active_iterator(StorageActiveIterator*);

GetStatus get_active_first_value(StorageActiveIterator*, char**);

GetStatus get_active_second_value(StorageActiveIterator*, char**);

SwapStatus swap_active_iterator(StorageActiveIterator*);
