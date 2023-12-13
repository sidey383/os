#include <sys/socket.h>

struct BufferEntry {
    char* buffer;
    int size;
    struct BufferEntry* next;
};

struct DataBuffer {
    int socket;
    struct BufferEntry* buffer_list;
    struct BufferEntry* current_buffer;
    size_t current_buffer_pose;
    size_t total_size;
};
