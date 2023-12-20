#define _GNU_SOURCE

#include <unistd.h>
#include "tcp.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/socket.h>

#define INT2VOIDP(i) (void*)(size_t)(i)
#define VOIDP2INT(v) (int)(size_t)(v)

#define POLL_TIMEOUT 200
#define LISTEN_QUEUE_SIZE 10
#define BUFFER_SIZE 32000

struct PollAction {
    enum EchoStatus (*func)(int, int, int *);

    short int flag;
};

void tcp_deconstruct_connection(Connection *con) {
    close(con->socket);
    free(con);
    debug("Connection: close socket %d", con->socket)
    debug("Free connection %p", con)
}

void tcp_deconstruct_connection_without_socket(Connection *con) {
    free(con);
    debug("Free connection %p", con)
}

TCPServer *tcp_create_server(struct sockaddr *socket_address, socklen_t address_len) {
    TCPServer *server = (TCPServer *) malloc(sizeof(TCPServer));
    int err;
    if (server == NULL) {
        fprintf(stderr, "Cannot allocate memory for a server\n");
        abort();
    }
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    // Always return 0 with argument PTHREAD_MUTEX_ERRORCHECK_NP
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK_NP);
    // Always return 0 with argument PTHREAD_PROCESS_PRIVATE
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_PRIVATE);
    // Always return 0
    pthread_mutex_init(&(server->mutex), &mutex_attr);
    // Always return 0
    pthread_mutexattr_destroy(&mutex_attr);
    server->socket = socket(socket_address->sa_family, SOCK_STREAM, 0);
    if (server->socket == -1) {
        //Not check errors
        //EBUSY  the mutex is currently locked - impossible
        pthread_mutex_destroy(&(server->mutex));
        free(server);
        return NULL;
    }
    debug("Open socket %d", server->socket)
    err = bind(server->socket, socket_address, address_len);
    if (err != 0) {
        //Not check errors
        //EBUSY  the mutex is currently locked - impossible
        pthread_mutex_destroy(&(server->mutex));
        close(server->socket);
        debug("Server create: close socket %d", server->socket)
        free(server);
        return NULL;
    }
    server->state = SERVER_NOT_STARTED;
    server->thread_list = create_thread_list((void (*)(void *)) tcp_deconstruct_connection);
    memcpy(&server->ip_address.address, &socket_address, sizeof(struct sockaddr));
    server->ip_address.address_len = address_len;
    return server;
}

static int busyMutexDestroy(pthread_mutex_t *mutex) {
    int err = 0;
    do {
        if (err == EBUSY)
            sched_yield();
        err = pthread_mutex_destroy(mutex);
    } while (err == EBUSY);
    return err;
}


int tcp_deconstruct_server(TCPServer *server, int *error) {
    int err;
    int serverState;

    err = pthread_mutex_lock(&server->mutex);
    if (err != 0)
        return err;
    serverState = server->state;
    server->state = SERVER_DECONSTRUCTED;
    pthread_mutex_unlock(&server->mutex);
    if (serverState == SERVER_ACTIVE || serverState == SERVER_FAIL) {
        err = pthread_cancel(server->main_thread);
        void *serverResult;
        if (err == 0) {
            err = pthread_join(server->main_thread, &serverResult);
        } else {
            fprintf(stderr, "tcp_deconstruct_server() error during pthread_cancel() %s", strerror(err));
            err = 0;
        }
        if (err == 0) {
            if (serverResult != PTHREAD_CANCELED)
                *error = VOIDP2INT(serverResult);
            else {
                *error = 0;
            }
        } else {
            fprintf(stderr, "tcp_deconstruct_server() error during pthread_join() %s", strerror(err));
            *error = 0;
        }
    }
    if (serverState != SERVER_DECONSTRUCTED) {
        close(server->socket);
        debug("Server deconstruct: close socket %d", server->socket)
    }
    if (server->thread_list != NULL) {
        err = deconstruct_thread_list(server->thread_list);
        if (err == 0) {
            server->thread_list = NULL;
        } else {
            return err;
        }
    }
    busyMutexDestroy(&server->mutex);
    free(server);
    return 0;
}

struct ServerArgs {
    TCPServer *server;
    AcceptFunc func;
};

/**
 * terminate server thread
 **/
static void failServerThread(TCPServer *server, int err) {
    int lockErr;
    lockErr = pthread_mutex_lock(&server->mutex);
    if (lockErr != 0) {
        fprintf(stderr, "TCP server can't lock mutex: %s\n", strerror(lockErr));
    } else {
        if (server->state == SERVER_ACTIVE)
            server->state = SERVER_FAIL;
        pthread_mutex_unlock(&server->mutex);
    }
    fprintf(stderr, "TCP server fail: %s\n", strerror(err));
    pthread_exit(INT2VOIDP(err));
}

static _Noreturn void *serverWorker(void *args) {
    struct ServerArgs *serverArgs = (struct ServerArgs *) args;
    TCPServer *server = serverArgs->server;
    void (*acceptFunc)(void *) = (void (*)(void *)) serverArgs->func;
    free(args);

    int err;
    err = listen(server->socket, LISTEN_QUEUE_SIZE);
    if (err != 0)
        failServerThread(server, errno);
    while (1) {
        int isHandled = 1;
        int clientSocket;
        Connection *con = (Connection *) malloc(sizeof(Connection));
        if (con == NULL) {
            fprintf(stderr, "serverWorker() can't allocate memory");
            abort();
        }
        debug("serverWorker() create clint connection %p", con)
        pthread_cleanup_push((void (*)(void *)) tcp_deconstruct_connection_without_socket, con)
                clientSocket = accept(server->socket, &con->ip_address.address, &(server->ip_address.address_len));
                if (clientSocket != -1) {
                    con->socket = clientSocket;
                    err = add_thread_to_list(server->thread_list, acceptFunc, con, NULL);
                    isHandled = err != 0;
                } else {
                    failServerThread(server, errno);
                }
        pthread_cleanup_pop(isHandled);
        debug("Open client socket %d", clientSocket)
        if (err != 0) {
            fprintf(stderr, "Fail to create user connection thread: %s", strerror(err));
        }
    }
}

int tcp_start_server(TCPServer *server, AcceptFunc func) {
    int err;
    err = pthread_mutex_lock(&server->mutex);
    if (err != 0)
        return err;
    int isBusy = server->state != SERVER_NOT_STARTED;
    if (isBusy) {
        //Not check errors
        //EINVAL the mutex has not been properly initialized, but this function already lock this mutex - impossible
        //EPERM  the calling thread does not own the mutex - impossible
        pthread_mutex_unlock(&server->mutex);
        return EBUSY;
    }
    struct ServerArgs *args = malloc(sizeof(struct ServerArgs));
    if (args == NULL) {
        pthread_mutex_unlock(&server->mutex);
        fputs("tcp_start_server() can't allocate memory", stderr);
        abort();
    }
    args->server = server;
    args->func = func;
    err = pthread_create(&server->main_thread, NULL, serverWorker, args);
    if (err != 0) {
        free(args);
        //Not check errors
        //EINVAL the mutex has not been properly initialized, but this function already lock this mutex - impossible
        //EPERM  the calling thread does not own the mutex - impossible
        pthread_mutex_unlock(&server->mutex);
        return err;
    }
    server->state = SERVER_ACTIVE;
    //Not check errors
    //EINVAL the mutex has not been properly initialized, but this function already lock this mutex - impossible
    //EPERM  the calling thread does not own the mutex - impossible
    pthread_mutex_unlock(&server->mutex);
    return 0;
}

static enum EchoStatus tcp_echo_packet(int in, int out, int *error) {
    char buffer[BUFFER_SIZE];
    size_t recvSize = recv(in, buffer, BUFFER_SIZE, 0);
    if (recvSize == -1) {
        (*error) = errno;
        return ECHO_RECV_ERROR;
    }
    size_t sendSize = 0;
    while (sendSize < recvSize) {
        size_t s = send(out, buffer + sendSize, recvSize - sendSize, 0);
        if (s == -1) {
            (*error) = errno;
            return ECHO_SEND_ERROR;
        }
        sendSize += s;
    }
#ifdef DEBUG
    write(STDOUT_FILENO, buffer, recvSize);
#endif
    return ECHO_OK;
}

static enum EchoStatus tcp_echo_packet_priority(int in, int out, int *error) {
    char buffer[BUFFER_SIZE];
    size_t recvSize = recv(in, buffer, BUFFER_SIZE, MSG_OOB);
    if (recvSize == -1) {
        (*error) = errno;
        return ECHO_RECV_ERROR;
    }
    size_t sendSize = 0;
    while (sendSize < recvSize) {
        size_t s = send(out, buffer + sendSize, recvSize - sendSize, MSG_OOB);
        if (s == -1) {
            (*error) = errno;
            return ECHO_SEND_ERROR;
        }
        sendSize += s;
    }
    write(STDOUT_FILENO, buffer, recvSize);
    return ECHO_OK;
}

static enum EchoStatus tcp_socket_error(int in, int out, int *error) {
    socklen_t len = sizeof(int);
    int getError = getsockopt(in, SOL_SOCKET, SO_ERROR, error, &len);
    if (getError != 0) {
        (*error) = getError;
    }
    return ECHO_SOCKET_ERROR;
}

static enum EchoStatus tcp_socket_close(int in, int out, int *error) {
    return ECHO_SOCKET_CLOSED;
}

static enum EchoStatus tcp_socket_wrong(int in, int out, int *error) {
    return ECHO_SOCKET_WRONG;
}

EchoStatus tcp_sockets_echo(int socket1, int socket2, int *error) {
    int status;
    enum EchoStatus result;
    struct PollAction actions[] = {
            {tcp_socket_wrong,         POLLNVAL},
            {tcp_socket_error,         POLLERR},
            {tcp_echo_packet_priority, POLLPRI},
            {tcp_echo_packet,          POLLIN},
            {tcp_socket_close,         POLLHUP | POLLRDHUP}
    };

    struct pollfd polls[2] = {
            {socket1, POLLIN | POLLRDHUP, 0},
            {socket2, POLLIN | POLLRDHUP, 0}
    };
    while (1) {
        status = poll(polls, 2, POLL_TIMEOUT);
        if (status == -1)
            return ECHO_POLL_ERROR;
        for (int p = 0; p < sizeof(actions) / sizeof(actions[0]); p++) {
            for (int i = 0; i < 2; i++) {
                struct pollfd *in = polls + i;
                struct pollfd *out = polls + ((i + 1) % 2);
                if ((in->revents & actions[p].flag) != 0) {
                    in->revents = (short) (in->revents & (~actions[p].flag));
                    result = actions[p].func(in->fd, out->fd, error);
                    if (result != ECHO_OK)
                        return result;
                }
            }
        }
        if (polls[0].revents != 0) {
            fprintf(stderr, "Not all poll event were intercepted: %d\n", polls[0].revents);
            polls[0].revents = 0;
        }
        if (polls[1].revents != 0) {
            fprintf(stderr, "Not all poll event were intercepted: %d\n", polls[1].revents);
            polls[1].revents = 0;
        }


    }
}

SelectStatus tcp_select_addr(const char *address, uint16_t port, IpAddress *result, int *error) {
    char portStr[16];
    sprintf(portStr, "%"PRIu16, port);
    return tcp_select_addr_str_port(address, portStr, result, error);
}

SelectStatus tcp_select_addr_str_port(const char *address, const char *port, IpAddress *result, int *error) {
    struct addrinfo *addrInfoList;
    struct addrinfo hints;
    int err;
    int found = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ALL;
    err = getaddrinfo(address, port, &hints, &addrInfoList);
    if (err != 0) {
        (*error) = err;
        return SELECT_ERROR;
    }
    for (struct addrinfo *i = addrInfoList; i != NULL; i = i->ai_next) {
        if (i->ai_family != AF_INET6 && i->ai_family != AF_INET)
            continue;
        if (result->address.sa_family == AF_INET && i->ai_family == AF_INET6)
            continue;
        result->address = *i->ai_addr;
        result->address_len = i->ai_addrlen;
        found = 1;
    }
    if (!found) {
        return SELECT_NO_RESULT;
    }
    freeaddrinfo(addrInfoList);
    return SELECT_OK;
}

char *tpc_address_to_string(struct sockaddr *addr, char *str, size_t strSize) {
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *) addr;
        const char *res = inet_ntop(AF_INET, &(a->sin_addr), str, strSize);
        if (res == NULL)
            return NULL;
        size_t p = strlen(str);
        snprintf(str + p, strSize - p, ":%"PRIu16, ntohs(a->sin_port));
    }
    if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *a6 = (struct sockaddr_in6 *) addr;
        const char *res = inet_ntop(AF_INET6, &(a6->sin6_addr), str, 256);
        if (res == NULL)
            return NULL;
        size_t p = strlen(str);
        snprintf(str + p, strSize - p, ":%"PRIu16, ntohs(a6->sin6_port));
    }
    return str;
}


//TODO: fix segfault?
Connection *tcp_create_connection(IpAddress *address, int *error) {
    Connection *con = (Connection *) malloc(sizeof(Connection));
    int status = -1;
    if (con == NULL) {
        fprintf(stderr, "Cannot allocate memory for a connection\n");
        abort();
    }
    debug("Create connection %p", con)
    con->socket = -1;
    pthread_cleanup_push((void (*)(void *)) tcp_deconstruct_connection_without_socket, con)
            con->ip_address = *address;
            con->socket = socket(con->ip_address.address.sa_family, SOCK_STREAM, 0);
            if (con->socket == -1) {
                (*error) = errno;
                tcp_deconstruct_connection_without_socket(con);
                return NULL;
            }
    pthread_cleanup_pop(0);
    pthread_cleanup_push((void (*)(void *)) tcp_deconstruct_connection, con)
            status = connect(con->socket, &address->address, address->address_len);
            if (status == -1) {
                debug("Socket connect error %s", strerror(errno))
                (*error) = errno;
            } else {
                debug("Connect to socket %d", con->socket)
            }
    pthread_cleanup_pop(0);
    if (status == -1) {
        con = NULL;
    }
    return con;
}
