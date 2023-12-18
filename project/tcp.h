#pragma once
#include "debug.h"
#include "threadList.h"
#include <pthread.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#define LISTEN_QUEUE_SIZE 10

/*
 * Contain resources
 * - socket
 * - mutex
 * - thread_list
 * */
#define SERVER_NOT_STARTED 0
/*
 * Contain resources
 * - socket
 * - mutex
 * - thread_list
 * - main_thread
 * */
#define SERVER_ACTIVE 1
/*
 * Contain resources
 * - socket
 * - mutex
 * - thread_list
 * - main_thread
 * */
#define SERVER_FAIL 2
/*
 * Contain resources
 * - mutex
 * - thread_list (Possible)
 * */
#define SERVER_DECONSTRUCTED 3

typedef struct ClientConnection {
    struct sockaddr socket_address;
    socklen_t address_len;
    int socket;
} ClientConnection;

typedef void (*AcceptFunc) (ClientConnection*);

typedef struct TCPServer {
    pthread_t main_thread;
    int socket;
    int state;
    ThreadList *thread_list;
    pthread_mutex_t mutex;
    struct sockaddr socket_address;
    socklen_t address_len;
} TCPServer;

struct TCPServer* create_server(struct sockaddr* socket_address, socklen_t address_len);

int deconstruct_server(TCPServer* server, int* error);

int start_server(TCPServer* server, AcceptFunc func);
