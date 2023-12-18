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


enum ReadStatus {
    READ_OK = 0,
    READ_ERROR = -1,
    READ_SOCKET_ERROR = -2,
    READ_SOCKET_WRONG = -3,
    READ_POLL_ERROR = -4,
    READ_SOCKET_CLOSE = -5
};

#define IS_READ_SET_ERROR(status) (\
status == READ_ERROR ||            \
status == READ_SOCKET_ERROR ||     \
status == READ_POLL_ERROR          \
)

enum ParamReadStatus {
    PARAM_NO_VALUE = 1,
    PARAM_WRONG_DATA = 2,
    PARAM_OK = 0,
    PARAM_READ_ERROR = -1,
    PARAM_READ_SOCKET_ERROR = -2,
    PARAM_READ_SOCKET_WRONG = -3,
    PARAM_READ_POLL_ERROR = -4,
    PARAM_READ_SOCKET_CLOSE = -5
};

#define IS_PARAM_READ_ERROR(status) (\
status == PARAM_READ_ERROR ||        \
status == PARAM_READ_SOCKET_ERROR || \
status == PARAM_READ_SOCKET_WRONG || \
status == PARAM_READ_POLL_ERROR ||   \
status == PARAM_READ_SOCKET_CLOSE    \
)


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

static void init_buffer(struct Buffer *buffer, int socket) {
    buffer->available = 0;
    buffer->iterator = 0;
    buffer->socket = socket;
    buffer->data = (char *) malloc(sizeof(char) * BUFFER_LENGTH);
    if (buffer->data == NULL) {
        fprintf(stderr, "Can't allocate memory for buffer, abort");
        abort();
    }
    buffer->data_len = BUFFER_LENGTH;
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

static void increase_buffer(struct Buffer *buffer) {
    size_t newLen = buffer->data_len * 3 / 2 + 1;
    if (newLen < BUFFER_LENGTH)
        newLen = BUFFER_LENGTH;
    char *newData = (char *) malloc(sizeof(char) * newLen);
    if (newData == NULL) {
        fprintf(stderr, "Can't allocate memory for data buffer, abort");
        abort();
    }
    memcpy(newData, buffer->data, buffer->available);
    memset(newData + buffer->available, 0, newLen - buffer->available);
    free(buffer->data);
    buffer->data = newData;
    buffer->data_len = newLen;
}

enum ReadStatus read_buffer(struct Buffer *buffer, int *err) {
    struct pollfd polls = {buffer->socket, POLLIN, 0};
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
    if ((polls.revents & POLLHUP) != 0) {
        polls.revents = 0;
        debug("Socket %d close", buffer->socket)
        return READ_SOCKET_CLOSE;
    }
    if ((polls.revents & POLLIN) != 0) {
        if (buffer->available >= buffer->data_len)
            increase_buffer(buffer);
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

static void clean_request_raw(void *p) {
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
        fprintf(stderr, "Can't allocate memory for parse http, abort");
        abort();
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
    size_t valueSize ;
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
    if (valueSize > 0 && request->buffer.data[lineValue.pose + valueSize - 1] == ' ') {
        valueSize--;
    }
    struct HttpParameterRaw *param = create_param(request);
    param->value.size = valueSize;
    param->value.pose = lineValue.pose + valueStart;
    param->name.size = nameSize;
    param->name.pose = lineValue.pose;
    return PARAM_OK;
}

void build_request(struct HttpRequestRaw* rawData, HttpRequest* data) {
    data->request_size = rawData->buffer.iterator;
    data->request = peek_buffer(&rawData->buffer);
    data->protocol = data->request + rawData->protocol.pose;
    data->protocol_size = rawData->protocol.size;
    data->uri = data->request + rawData->uri.pose;
    data->uri_size = rawData->uri.size;
    data->method = data->request + rawData->method.pose;
    data->method_size = rawData->method.size;
    HttpParameter** paramPlace = &data->parameters;
    for (struct HttpParameterRaw* rp = rawData->parameters; rp != NULL; rp = rp->next) {
        HttpParameter* param = (HttpParameter*) malloc(sizeof(HttpParameter));
        if (param == NULL) {
            fprintf(stderr, "Can't allocate memory for parse http, abort");
            abort();
        }
        param->next = NULL;
        param->name = data->request + rp->name.pose;
        param->name_size = rp->name.size;
        param->value = data->request + rp->value.pose;
        param->value_size = rp->value.size;
        (*paramPlace) = param;
        paramPlace = &param->next;
    }
}

enum AcceptStatus accept_request(int socket, HttpRequest *data) {
    struct HttpRequestRaw requestRaw = {};
    enum ReadStatus readStatus;
    enum ParamReadStatus paramStatus;
    int err;
    pthread_cleanup_push(clean_request_raw, &requestRaw)
            init_buffer(&requestRaw.buffer, socket);
            readStatus = read_buffer(&requestRaw.buffer, &err);
            if (readStatus == READ_SOCKET_CLOSE) {
                clean_request_raw(&requestRaw);
                return ACCEPT_SOCKET_CLOSE;
            }
            if (IS_READ_SET_ERROR(readStatus)) {
                fprintf(stderr, "Socket error: %s", strerror(err));
            }
            if (readStatus != READ_OK) {
                return ACCEPT_ERROR;
            }
            while (!parse_space_value(&requestRaw.method, &requestRaw.buffer)) {
                readStatus = read_buffer(&requestRaw.buffer, &err);
                if (readStatus == READ_SOCKET_CLOSE) {
                    clean_request_raw(&requestRaw);
                    return ACCEPT_SOCKET_CLOSE;
                }
                if (IS_READ_SET_ERROR(readStatus)) {
                    fprintf(stderr, "Socket error: %s", strerror(err));
                }
                if (readStatus != READ_OK) {
                    return ACCEPT_ERROR;
                }
            }
            while (!parse_space_value(&requestRaw.uri, &requestRaw.buffer)) {
                readStatus = read_buffer(&requestRaw.buffer, &err);
                if (readStatus == READ_SOCKET_CLOSE) {
                    clean_request_raw(&requestRaw);
                    return ACCEPT_SOCKET_CLOSE;
                }
                if (IS_READ_SET_ERROR(readStatus)) {
                    fprintf(stderr, "Socket error: %s", strerror(err));
                }
                if (readStatus != READ_OK) {
                    return ACCEPT_ERROR;
                }
            }
            while (!parse_end_line_value(&requestRaw.protocol, &requestRaw.buffer)) {
                readStatus = read_buffer(&requestRaw.buffer, &err);
                if (readStatus == READ_SOCKET_CLOSE) {
                    clean_request_raw(&requestRaw);
                    return ACCEPT_SOCKET_CLOSE;
                }
                if (IS_READ_SET_ERROR(readStatus)) {
                    fprintf(stderr, "Socket error: %s", strerror(err));
                }
                if (readStatus != READ_OK) {
                    return ACCEPT_ERROR;
                }
            }
            do {
                paramStatus = read_param(&requestRaw, &err);
                if (paramStatus == PARAM_READ_SOCKET_CLOSE) {
                    clean_request_raw(&requestRaw);
                    return ACCEPT_SOCKET_CLOSE;
                }
                if (IS_READ_SET_ERROR(paramStatus)) {
                    fprintf(stderr, "Socket error: %s", strerror(err));
                }
                if (IS_PARAM_READ_ERROR(paramStatus) || paramStatus == PARAM_WRONG_DATA) {
                    clean_request_raw(&requestRaw);
                    return ACCEPT_ERROR;
                }
            } while (paramStatus != PARAM_NO_VALUE);
            build_request(&requestRaw, data);
    pthread_cleanup_pop(1);
    return ACCEPT_OK;
}

void deconstruct_request(HttpRequest* request) {
    free(request->request);
    for (HttpParameter* p = request->parameters; p != NULL;) {
        HttpParameter* tmp = p->next;
        free(p);
        p = tmp;
    }
}
