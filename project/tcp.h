#pragma once
#include <pthread.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#define LISTEN_QUEUE_SIZE 10
#define SERVER_NOT_STARTED 0
#define SERVER_ACTIVE 1
#define SERVER_FAIL 2
#define SERVER_DECONSTRUCT 3

typedef struct address {
    socklen_t address_len;
    int family;
    struct sockaddr socket_address;
} address;

typedef struct ClientConnection {
    address address;
    int socket;
} ClientConnection;

typedef void (*AcceptFunc) (ClientConnection);

typedef struct UserThreadNode {
    pthread_t thread;
    int socket;
    address address;
    struct UserThreadNode* next;
    struct UserThreadNode* prev;
} UserThreadNode;

typedef struct UserThreadList {
    UserThreadNode* first;
    pthread_mutex_t lock;
    int isDeconstructed;
} UserThreadList;

typedef struct TCPServer {
    pthread_t main_thread;
    int socket;
    int state;
    int error;
    UserThreadList user_list;
    pthread_mutex_t lock;
    address address;
} TCPServer;

struct TCPServer* createServer(const address* addr);

int deconstructServer(TCPServer* server);

int startServer(TCPServer* server, AcceptFunc func);
