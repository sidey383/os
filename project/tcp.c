#define DEBUG

#include <unistd.h>
#include "tcp.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define INT2VOIDP(i) (void*)(size_t)(i)
#define VOIDP2INT(v) (int)(size_t)(v)

void freeConnection(void *val) {
    ClientConnection *con = (ClientConnection *) val;
    close(con->socket);
    free(con);
    debug("Connection: close socket %d", con->socket)
    debug("Free connection %p", val)
}

void freeConnectionServer(void *val) {
    ClientConnection *con = (ClientConnection *) val;
    free(con);
    debug("Free connection server %p", val)
}

TCPServer *create_server(struct sockaddr *socket_address, socklen_t address_len) {
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
    server->thread_list = create_thread_list(freeConnection);
    memcpy(&server->socket_address, &socket_address, sizeof(struct sockaddr));
    server->address_len = address_len;
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


int deconstruct_server(TCPServer *server, int *error) {
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
            fprintf(stderr, "deconstruct_server() error during pthread_cancel() %s", strerror(err));
            err = 0;
        }
        if (err == 0) {
            if (serverResult != PTHREAD_CANCELED)
                *error = VOIDP2INT(serverResult);
            else {
                *error = 0;
            }
        } else {
            fprintf(stderr, "deconstruct_server() error during pthread_join() %s", strerror(err));
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
 * terminate thread
 **/
void failServerThread(TCPServer *server, int err) {
    int lockErr;
    lockErr = pthread_mutex_lock(&server->mutex);
    if (lockErr != 0) {
        fprintf(stderr, "TCP server can't lock mutex: %s\n", strerror(lockErr));
    } else {
        if (server->state == SERVER_ACTIVE)
            server->state = SERVER_FAIL;
        pthread_mutex_unlock(&server->mutex);
    }
    //Not check errors
    //EINVAL the mutex has not been properly initialized, but this function already lock this mutex - impossible
    //EPERM  the calling thread does not own the mutex - impossible
    fprintf(stderr, "TCP server fail: %s\n", strerror(err));
    pthread_exit(INT2VOIDP(err));
}

_Noreturn void *serverWorker(void *args) {
    struct ServerArgs *serverArgs = (struct ServerArgs *) args;
    TCPServer *server = serverArgs->server;
    void (*acceptFunc)(void *) = (void (*)(void *)) serverArgs->func;
    free(args);

    int err;
    err = listen(server->socket, LISTEN_QUEUE_SIZE);
    if (err != 0) {
        failServerThread(server, err);
        pthread_exit(INT2VOIDP(-1));
    }
    while (1) {
        int isHandled = 1;
        int clientSocket = -1;
        ClientConnection *con = (ClientConnection *) malloc(sizeof(ClientConnection));
        if (con == NULL) {
            fprintf(stderr, "serverWorker() can't allocate memory");
            abort();
        }
        debug("serverWorker() create clint connection %p", con);
        pthread_cleanup_push(freeConnectionServer, con)
                clientSocket = accept(server->socket, &con->socket_address, &(server->address_len));
                if (clientSocket != -1) {
                    con->socket = clientSocket;
                    err = add_thread_to_list(server->thread_list, acceptFunc, con, NULL);
                    isHandled = 0;
                    if (err == 0) {
                        isHandled = 0;
                    }
                } else {
                    failServerThread(server, errno);
                    pthread_exit(INT2VOIDP(1));
                }
        pthread_cleanup_pop(isHandled);
        debug("Open client socket %d", clientSocket);
        if (err != 0) {
            fprintf(stderr, "Fail to create user connection thread: %s", strerror(err));
        }
    }
}

int start_server(TCPServer *server, AcceptFunc func) {
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
        fputs("start_server() can't allocate memory", stderr);
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
