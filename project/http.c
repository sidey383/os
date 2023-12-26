#define _GNU_SOURCE

#include <unistd.h>
#include <sys/socket.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http.h"
#include <pthread.h>

#define SPACE ' '

#define POLL_TIMEOUT 100
#define BUFFER_LENGTH 1024

enum ReadStatus {
    READ_OK = 0,
    READ_ERROR = -1,
    READ_SOCKET_ERROR = -2,
    READ_SOCKET_WRONG = -3,
    READ_POLL_ERROR = -4,
    READ_SOCKET_CLOSE = -5,
    READ_NO_MEMORY = -6
};

#define IS_READ_SET_ERROR(status) (\
status == READ_ERROR ||            \
status == READ_SOCKET_ERROR ||     \
status == READ_POLL_ERROR ||       \
status == READ_NO_MEMORY \
)

enum ParamReadStatus {
    PARAM_NO_MEMORY = 3,
    PARAM_WRONG_DATA = 2,
    PARAM_NO_VALUE = 1,
    PARAM_OK = 0,
    PARAM_READ_ERROR = -1,
    PARAM_READ_SOCKET_ERROR = -2,
    PARAM_READ_SOCKET_WRONG = -3,
    PARAM_READ_POLL_ERROR = -4,
    PARAM_READ_SOCKET_CLOSE = -5,
    PARAM_READ_NO_MEMORY = -6
};

#define IS_PARAM_READ_ERROR(status) (\
status == PARAM_READ_ERROR ||        \
status == PARAM_READ_SOCKET_ERROR || \
status == PARAM_READ_SOCKET_WRONG || \
status == PARAM_READ_POLL_ERROR ||   \
status == PARAM_READ_SOCKET_CLOSE || \
status == PARAM_READ_NO_MEMORY )

#define HANDLE_READ_STATUS(readStatus, requestRaw) { \
    if (readStatus == READ_SOCKET_CLOSE) { \
        clean_request_raw(&requestRaw);\
        return ACCEPT_SOCKET_CLOSE; \
    } \
    if (readStatus == READ_NO_MEMORY) { \
        clean_request_raw(&requestRaw); \
        return ACCEPT_NO_MEMORY;                             \
    }\
    if (readStatus != READ_OK) { \
        clean_request_raw(&requestRaw); \
        return ACCEPT_ERROR; \
    }                                              \
}

struct Buffer {
    char *data;
    size_t data_len;
    size_t available;
    size_t iterator;
    int socket;
};

struct Value {
    size_t pose;
    size_t size;
};

struct HttpParameterRaw {
    struct Value name;
    struct Value value;
    struct HttpParameterRaw *next;
};

struct HttpRequestRaw {
    struct Buffer buffer;
    struct Value method;
    struct Value uri;
    struct Value protocol;
    struct HttpParameterRaw *parameters;
};

static int init_buffer(struct Buffer *buffer, int socket) {
    buffer->available = 0;
    buffer->iterator = 0;
    buffer->socket = socket;
    buffer->data = (char *) malloc(sizeof(char) * BUFFER_LENGTH);
    if (buffer->data == NULL) {
        return ENOMEM;
    }
    buffer->data_len = BUFFER_LENGTH;
    return 0;
}

static void clean_buffer(struct Buffer *buffer) {
    buffer->data_len = 0;
    buffer->available = 0;
    buffer->iterator = 0;
    buffer->socket = 0;
    if (buffer->data != NULL)
        free(buffer->data);
    buffer->data = NULL;
}

static char *peek_buffer(struct Buffer *buffer) {
    char *data = buffer->data;
    buffer->data_len = 0;
    buffer->available = 0;
    buffer->iterator = 0;
    buffer->socket = 0;
    buffer->data = NULL;
    return data;
}

static int increase_buffer(struct Buffer *buffer) {
    size_t newLen = buffer->data_len * 3 / 2 + 1;
    if (newLen < BUFFER_LENGTH)
        newLen = BUFFER_LENGTH;
    char *newData = (char *) malloc(sizeof(char) * newLen);
    if (newData == NULL) {
        return ENOMEM;
    }
    memcpy(newData, buffer->data, buffer->available);
    memset(newData + buffer->available, 0, newLen - buffer->available);
    free(buffer->data);
    buffer->data = newData;
    buffer->data_len = newLen;
    return 0;
}

enum ReadStatus read_buffer(struct Buffer *buffer, int *err) {
    struct pollfd polls = {buffer->socket, POLLIN | POLLRDHUP, 0};
    int status;
    status = poll(&polls, 1, POLL_TIMEOUT);
    if (status == -1) {
        (*err) = errno;
        return READ_POLL_ERROR;
    }
    if (status == 0)
        return READ_OK;
    if ((polls.revents & POLLERR) != 0) {
        polls.revents = 0;
        int internalError;
        socklen_t len = sizeof(int);
        internalError = getsockopt(buffer->socket, SOL_SOCKET, SO_ERROR, err, &len);
        if (internalError != 0) {
            debug("Socket %d error, but can't get error: %s", buffer->socket, strerror(internalError))
            (*err) = internalError;
        }
        debug("Socket %d error: %s", buffer->socket, strerror(*err))
        return READ_SOCKET_ERROR;
    }
    if ((polls.revents & POLLNVAL) != 0) {
        polls.revents = 0;
        debug("Socket %d wrong", buffer->socket)
        return READ_SOCKET_WRONG;
    }
    if ((polls.revents & (POLLHUP | POLLRDHUP)) != 0) {
        polls.revents = 0;
        debug("Socket %d closed", buffer->socket)
        return READ_SOCKET_CLOSE;
    }
    if ((polls.revents & POLLIN) != 0) {
        if (buffer->available >= buffer->data_len) {
            status = increase_buffer(buffer);
            if (status != 0) {
                (*err) = ENOMEM;
                return READ_NO_MEMORY;
            }
        }
        polls.revents = (short) (polls.revents & (~POLLIN));
        size_t size = recv(buffer->socket, buffer->data + buffer->available, buffer->data_len - buffer->available, 0);
        if (size == -1) {
            (*err) = errno;
            return READ_ERROR;
        }
        buffer->available += size;
        debug("Socket %d read data %zu", buffer->socket, size)
    }
    return READ_OK;
}

void clean_request_raw(void *p) {
    struct HttpRequestRaw *r = (struct HttpRequestRaw *) p;
    while (r->parameters != NULL) {
        void *pointer = r->parameters;
        free(pointer);
        r->parameters = r->parameters->next;
    }
    clean_buffer(&r->buffer);
}

void clean_request_raw_ok(void *p) {
    struct HttpRequestRaw *r = (struct HttpRequestRaw *) p;
    while (r->parameters != NULL) {
        void *pointer = r->parameters;
        free(pointer);
        r->parameters = r->parameters->next;
    }
    clean_buffer(&r->buffer);
}

int parse_space_value(struct Value *value, struct Buffer *buffer) {
    int isEnd = 0;
    while (buffer->iterator < buffer->available && !isEnd) {
        if (value->size == 0)
            value->pose = buffer->iterator;
        char c = buffer->data[buffer->iterator];
        isEnd = c == SPACE;
        if (!isEnd)
            value->size++;
        buffer->iterator++;
    }
    return isEnd;
}

int parse_end_line_value(struct Value *value, struct Buffer *buffer) {
    int isCR = 0;
    int isEnd = 0;
    while (buffer->iterator < buffer->available && !isEnd) {
        if (value->size == 0)
            value->pose = buffer->iterator;
        char c = buffer->data[buffer->iterator];
        isEnd = isCR && c == '\n';
        isCR = c == '\r';
        if (!isEnd && !isCR)
            value->size++;
        buffer->iterator++;
    }
    if (isCR)
        buffer->iterator--;
    return isEnd;
}

struct HttpParameterRaw *create_param(struct HttpRequestRaw *request) {
    struct HttpParameterRaw **last = &(request->parameters);
    while (*last != NULL) {
        last = &((*last)->next);
    }
    struct HttpParameterRaw *newParam = (struct HttpParameterRaw *) malloc(sizeof(struct HttpParameterRaw));
    if (newParam == NULL) {
        return NULL;
    }
    newParam->next = NULL;
    (*last) = newParam;
    return newParam;
}

enum ParamReadStatus read_param(struct HttpRequestRaw *request, int *error) {
    struct Value lineValue = {};
    int err;
    enum ReadStatus readStatus;
    size_t nameSize;
    size_t valueStart;
    size_t valueSize;
    // Select full line
    while (!parse_end_line_value(&lineValue, &request->buffer)) {
        readStatus = read_buffer(&request->buffer, &err);
        if (IS_READ_SET_ERROR(readStatus))
            (*error) = err;
        if (readStatus != READ_OK)
            return (enum ParamReadStatus) readStatus;
    }
    // Line empty
    if (lineValue.size == 0)
        return PARAM_NO_VALUE;
    //Find name size by ':'
    nameSize = 0;
    while (nameSize < lineValue.size) {
        if (request->buffer.data[lineValue.pose + nameSize] == ':')
            break;
        nameSize++;
    }
    //Can't found ':'
    if (nameSize == lineValue.size)
        return PARAM_WRONG_DATA;
    //Next character after ':' is start of value
    valueStart = nameSize + 1;
    valueSize = lineValue.size - valueStart;
    //Ignore first ' ' if it exists
    if (valueSize > 0 && request->buffer.data[lineValue.pose + valueStart] == ' ') {
        valueSize--;
        valueStart++;
    }
    //Ignore last ' ' if it exists
    if (valueSize > 0 && request->buffer.data[lineValue.pose + valueStart + valueSize - 1] == ' ') {
        valueSize--;
    }
    struct HttpParameterRaw *param = create_param(request);
    if (param == NULL)
        return PARAM_NO_MEMORY;
    param->value.size = valueSize;
    param->value.pose = lineValue.pose + valueStart;
    param->name.size = nameSize;
    param->name.pose = lineValue.pose;
    return PARAM_OK;
}

int build_request(struct HttpRequestRaw *rawData, HttpRequest *data) {
    data->request_size = rawData->buffer.iterator;
    data->request = peek_buffer(&rawData->buffer);
    data->protocol = data->request + rawData->protocol.pose;
    data->protocol_size = rawData->protocol.size;
    data->uri = data->request + rawData->uri.pose;
    data->uri_size = rawData->uri.size;
    data->method = data->request + rawData->method.pose;
    data->method_size = rawData->method.size;
    HttpParameter **paramPlace = &data->parameters;
    for (struct HttpParameterRaw *rp = rawData->parameters; rp != NULL; rp = rp->next) {
        HttpParameter *param = (HttpParameter *) malloc(sizeof(HttpParameter));
        if (param == NULL) {
            return ENOMEM;
        }
        param->next = NULL;
        param->name = data->request + rp->name.pose;
        param->name_size = rp->name.size;
        param->value = data->request + rp->value.pose;
        param->value_size = rp->value.size;
        (*paramPlace) = param;
        paramPlace = &param->next;
    }
    return 0;
}

static int equals_string(const char *str1, size_t size1, const char *str2, size_t size2) {
    if (size1 != size2) {
        return 0;
    }
    for (size_t i = 0; i < size1; i++) {
        if (str1[i] != str2[i])
            return 0;
    }
    return 1;
}

static size_t read_size(const char *val, size_t size) {
    size_t result = 0;
    for (int i = 0; i < size; i++) {
        char c = val[i];
        if (c < '0' || c > '9')
            return -1;
        result = result * 10 + c - '0';
    }
    return result;
}

enum ReadStatus add_content(struct HttpRequestRaw *rawData, int *error) {
    struct HttpParameterRaw *dataLenParam = NULL;
    enum ReadStatus status;
    for (struct HttpParameterRaw *p = rawData->parameters; p != NULL; p = p->next) {
        if (equals_string(rawData->buffer.data + p->name.pose, p->name.size, "Content-Length", 14)) {
            dataLenParam = p;
        }
    }
    size_t dataLength = 0;
    if (dataLenParam != NULL) {
        dataLength = read_size(rawData->buffer.data + dataLenParam->value.pose, dataLenParam->value.size);
    }
    if (dataLength == 0)
        return READ_OK;
    while (rawData->buffer.available - rawData->buffer.iterator < dataLength) {
        status = read_buffer(&rawData->buffer, error);
        if (status != READ_OK)
            return status;
    }
    rawData->buffer.iterator += dataLength;
    return READ_OK;
}

enum AcceptStatus accept_request(int socket, HttpRequest *data) {
    struct HttpRequestRaw requestRaw = {};
    enum ReadStatus readStatus;
    enum ParamReadStatus paramStatus;
    int err;
    pthread_cleanup_push(clean_request_raw_ok, &requestRaw)
            err = init_buffer(&requestRaw.buffer, socket);
            if (err != 0) {
                return ACCEPT_NO_MEMORY;
            }
            readStatus = read_buffer(&requestRaw.buffer, &err);
            HANDLE_READ_STATUS(readStatus, requestRaw)
            while (!parse_space_value(&requestRaw.method, &requestRaw.buffer)) {
                readStatus = read_buffer(&requestRaw.buffer, &err);
                HANDLE_READ_STATUS(readStatus, requestRaw)
            }
            while (!parse_space_value(&requestRaw.uri, &requestRaw.buffer)) {
                readStatus = read_buffer(&requestRaw.buffer, &err);
                HANDLE_READ_STATUS(readStatus, requestRaw)
            }
            while (!parse_end_line_value(&requestRaw.protocol, &requestRaw.buffer)) {
                readStatus = read_buffer(&requestRaw.buffer, &err);
                HANDLE_READ_STATUS(readStatus, requestRaw)
            }
            do {
                paramStatus = read_param(&requestRaw, &err);
                if (paramStatus == PARAM_READ_SOCKET_CLOSE) {
                    clean_request_raw(&requestRaw);
                    return ACCEPT_SOCKET_CLOSE;
                }
                if (paramStatus == PARAM_NO_MEMORY) {
                    clean_request_raw(&requestRaw);
                    return ACCEPT_NO_MEMORY;
                }
                if (IS_READ_SET_ERROR(paramStatus)) {
                    fprintf(stderr, "Socket error: %s", strerror(err));
                }
                if (IS_PARAM_READ_ERROR(paramStatus) || paramStatus == PARAM_WRONG_DATA) {
                    clean_request_raw(&requestRaw);
                    return ACCEPT_ERROR;
                }
            } while (paramStatus != PARAM_NO_VALUE);
            readStatus = add_content(&requestRaw, &err);
            HANDLE_READ_STATUS(readStatus, requestRaw)
            err = build_request(&requestRaw, data);
            if (err == ENOMEM) {
                deconstruct_request(data);
                clean_request_raw(&requestRaw);
                return ACCEPT_NO_MEMORY;
            }
    pthread_cleanup_pop(1);
    return ACCEPT_OK;
}

void deconstruct_request(HttpRequest *request) {
    free(request->request);
    for (HttpParameter *p = request->parameters; p != NULL;) {
        HttpParameter *tmp = p->next;
        free(p);
        p = tmp;
    }
}
