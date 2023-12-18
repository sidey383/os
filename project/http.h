#pragma once
#define DEBUG
#include "debug.h"

#define POLL_TIMEOUT 100
#define BUFFER_LENGTH 64

typedef struct HttpParameter HttpParameter;

typedef struct HttpRequest HttpRequest;

struct HttpParameter {
    char* name;
    size_t name_size;
    char* value;
    size_t value_size;
    HttpParameter *next;
};

struct HttpRequest {
    char* request;
    size_t request_size;
    char* method;
    size_t method_size;
    char* uri;
    size_t uri_size;
    char* protocol;
    size_t protocol_size;
    HttpParameter *parameters;
};

enum AcceptStatus {
    ACCEPT_OK = 0,
    ACCEPT_ERROR = -1,
    ACCEPT_SOCKET_CLOSE = -2
};

enum AcceptStatus accept_request(int socket, HttpRequest* request);

void deconstruct_request(HttpRequest* request);
