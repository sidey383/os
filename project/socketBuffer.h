#pragma once
#include <sys/socket.h>

typedef struct BufferEntry BufferEntry;

typedef struct DataBuffer DataBuffer;

struct BufferEntry {
    char* buffer;
    int size;
    BufferEntry* next;
};

struct DataBuffer {
    int socket;
    size_t pose;
    BufferEntry* buffer_list;
    BufferEntry* current_buffer;
    size_t current_buffer_pose;
    size_t total_size;
};

struct DataBuffer* create_buffer(int socket);

size_t part_size(DataBuffer* buffer, char terminate);

void set_start_buffer(DataBuffer* buffer);
