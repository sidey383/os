#pragma once

//#define DEBUG

#include "debug.h"
#include "threadList.h"
#include <pthread.h>
#include <netdb.h>

typedef struct Connection Connection;
typedef struct TCPServer TCPServer;
typedef struct IpAddress IpAddress;
typedef void (*AcceptFunc)(Connection *);

typedef enum EchoStatus EchoStatus;
typedef enum SelectStatus SelectStatus;
typedef enum ServerStatus ServerStatus;

enum EchoStatus {
    ECHO_OK,
    ECHO_SOCKET_CLOSED,
    ECHO_SOCKET_ERROR,
    ECHO_SOCKET_WRONG,
    ECHO_RECV_ERROR,
    ECHO_SEND_ERROR,
    ECHO_POLL_ERROR
};

enum ServerStatus {
    /*
 * Contain resources
 * - socket
 * - mutex
 * - thread_list
 * */
    SERVER_NOT_STARTED = 0,
    /*
 * Contain resources
 * - socket
 * - mutex
 * - thread_list
 * - main_thread
 * */
    SERVER_ACTIVE = 1,
    /*
 * Contain resources
 * - socket
 * - mutex
 * - thread_list
 * - main_thread
 * */
    SERVER_FAIL = 2,
    /*
 * Contain resources
 * - mutex
 * - thread_list (Possible)
 * */
    SERVER_DECONSTRUCTED = 3
};

enum SelectStatus {
    SELECT_OK,
    SELECT_NO_RESULT,
    SELECT_ERROR
};

struct IpAddress {
    socklen_t address_len;
    struct sockaddr address;
};

struct Connection {
    IpAddress ip_address;
    int socket;
};

struct TCPServer {
    pthread_t main_thread;
    int socket;
    ServerStatus state;
    ThreadList *thread_list;
    pthread_mutex_t mutex;
    IpAddress ip_address;
};

EchoStatus tcp_sockets_echo(int socket1, int socket2, int *error);

SelectStatus tcp_select_addr(const char *address, uint16_t port, IpAddress *result, int *error);

SelectStatus tcp_select_addr_str_port(const char *address, const char* port, IpAddress *result, int *error);

char* tpc_address_to_string(struct sockaddr* addr, char* str, size_t strSize);

struct TCPServer *tcp_create_server(struct sockaddr *socket_address, socklen_t address_len);

int tcp_deconstruct_server(TCPServer *server, int *error);

int tcp_start_server(TCPServer *server, AcceptFunc func);

Connection* tcp_create_connection(IpAddress *address, int* error);

void tcp_deconstruct_connection(Connection *connection);
