#include <unistd.h>
#include "tcp.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

TCPServer *createServer(const struct address *addr) {
    TCPServer *server = (TCPServer *) malloc(sizeof(TCPServer));
    int err;
    if (server == NULL) {
        fprintf(stderr, "Cannot allocate memory for a server\n");
        abort();
    }
    server->state = SERVER_NOT_STARTED;
    server->user_list.first = NULL;
    server->user_list.isDeconstructed = 0;
    server->error = 0;
    server->address = *addr;
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    // Always return 0 with argument PTHREAD_MUTEX_ERRORCHECK_NP
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK_NP);
    // Always return 0 with argument PTHREAD_PROCESS_PRIVATE
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_PRIVATE);
    // Always return 0
    pthread_mutex_init(&(server->lock), &mutex_attr);
    pthread_mutex_init(&(server->user_list.lock), &mutex_attr);
    // Always return 0
    pthread_mutexattr_destroy(&mutex_attr);
    server->socket = socket(server->address.family, SOCK_STREAM, 0);
    if (server->socket == -1) {
        //Not check errors
        //EBUSY  the mutex is currently locked - impossible
        pthread_mutex_destroy(&(server->lock));
        pthread_mutex_destroy(&(server->user_list.lock));
        free(server);
        return NULL;
    }
    err = bind(server->socket, &(addr->socket_address), addr->address_len);
    if (err != 0) {
        //Not check errors
        //EBUSY  the mutex is currently locked - impossible
        pthread_mutex_destroy(&(server->lock));
        pthread_mutex_destroy(&(server->user_list.lock));
        close(server->socket);
        free(server);
        return NULL;
    }
    return server;
}

int busyMutexDestroy(pthread_mutex_t *mutex) {
    int err = 0;
    do {
        if (err == EBUSY)
            sched_yield();
        err = pthread_mutex_destroy(mutex);
    } while (err == EBUSY);
    return err;
}


int deconstructServer(TCPServer *server) {
    int err;
    err = pthread_cancel(server->main_thread);
    if (err != ESRCH && err != 0) {
        fprintf(stderr, "Main thread cancel return unexpected error %s\n", strerror(err));
        return err;
    }
    if (err == 0) {
        //TODO: Can't join detached threads....
        err = pthread_join(server->main_thread, NULL);
    } else {
        err = 0;
    }
    if (err != 0) {
        fprintf(stderr, "Main thread join error %s\n", strerror(err));
        return err;
    }
    err = pthread_mutex_lock(&server->lock);
    if (err != 0) {
        fprintf(stderr, "Can't lock server mutex for deconstruct mutex: %s\n", strerror(err));
        return err;
    }
    server->state = SERVER_DECONSTRUCT;
    pthread_mutex_unlock(&server->lock);
    err = busyMutexDestroy(&server->lock);
    if (err != 0) {
        fprintf(stderr, "Server mutex destroy return unexpected error %s\n", strerror(err));
        return err;
    }
    err = pthread_mutex_lock(&server->user_list.lock);
    if (err != 0) {
        fprintf(stderr, "Can't lock user list mutex for deconstruct mutex: %s\n", strerror(err));
        return err;
    }
    server->user_list.isDeconstructed = 1;
    pthread_mutex_unlock(&server->user_list.lock);
    for (UserThreadNode *i = server->user_list.first; i != NULL;) {
        err = pthread_cancel(i->thread);
        if (err != ESRCH && err != 0) {
            fprintf(stderr, "User thread cancel error %s", strerror(err));
            return err;
        }
        //TODO: Can't join detached threads....
        err = pthread_join(i->thread, NULL);
        if (err != 0) {
            fprintf(stderr, "User thread join error %s", strerror(err));
        }
        UserThreadNode *n = i->next;
        free(i);
        i = n;
    }
    do {
        if (err == EBUSY)
            sched_yield();
        err = pthread_mutex_destroy(&server->user_list.lock);
    } while (err == EBUSY);
    free(server);
    return 0;
}

struct AcceptorArgs {
    TCPServer *server;
    UserThreadNode *acceptorNode;
    AcceptFunc func;
};

struct ServerArgs {
    TCPServer *server;
    AcceptFunc func;
};

void removeUserThreadNodeNoblock(TCPServer *server, UserThreadNode *userNode) {
    if (userNode->prev == NULL) {
        server->user_list.first = userNode->next;
    } else {
        userNode->prev->next = userNode->next;
        if (userNode->next != NULL) {
            userNode->next->prev = userNode->prev;
        }
    }
    free(userNode);
}

int removeUserThreadNode(TCPServer *server, UserThreadNode *userNode) {
    int err;
    err = pthread_mutex_lock(&server->user_list.lock);
    if (err != 0)
        return err;
    if (server->user_list.isDeconstructed) {
        pthread_mutex_unlock(&server->user_list.lock);
        return 0;
    }
    removeUserThreadNodeNoblock(server, userNode);
    pthread_mutex_unlock(&server->user_list.lock);
    return 0;
}


void closeSocketPointer(void *socket) {
    close(*(int*)socket);
}

void *acceptor(void *args_) {
    struct AcceptorArgs *args = (struct AcceptorArgs *) args_;
    int err;
    UserThreadNode *userNode = args->acceptorNode;
    AcceptFunc func = args->func;
    TCPServer *server = args->server;
    free(args_);
    ClientConnection connection = {
            userNode->address,
            userNode->socket
    };
    int socketClear = userNode->socket;
    pthread_cleanup_push(closeSocketPointer, &socketClear)
            func(connection);
            err = removeUserThreadNode(server, userNode);
            if (err != 0)
                fprintf(stderr, "Fail to remove user list mutex: %s\n", strerror(err));
            pthread_detach(pthread_self());
    pthread_cleanup_pop(1);
    return NULL;
}

int addUserNode(TCPServer *server, address clientAddress, int clientSocket, AcceptFunc acceptFunc) {
    int err;
    UserThreadNode *userNode = (UserThreadNode *) malloc(sizeof(UserThreadNode));
    if (userNode == NULL) {
        fprintf(stderr, "Can't allocate memory fot new client node\n");
        close(clientSocket);
        abort();
    }
    struct AcceptorArgs *threadArgs = (struct AcceptorArgs *) malloc(sizeof(struct AcceptorArgs));
    if (threadArgs == NULL) {
        fprintf(stderr, "Can't allocate memory for client thread\n");
        close(clientSocket);
        free(userNode);
        abort();
    }
    userNode->socket = clientSocket;
    userNode->address = clientAddress;
    threadArgs->server = server;
    threadArgs->acceptorNode = userNode;
    threadArgs->func = acceptFunc;
    err = pthread_mutex_lock(&server->user_list.lock);
    if (err != 0) {
        free(userNode);
        close(clientSocket);
        fprintf(stderr, "Fail to lock user list mutex: %s\n", strerror(err));
        return err;
    }
    server->user_list.isDeconstructed = 0;
    userNode->prev = NULL;
    userNode->next = server->user_list.first;
    if (server->user_list.first != NULL) {
        server->user_list.first->prev = userNode;
    }
    server->user_list.first = userNode;
    err = pthread_create(&(userNode->thread), NULL, acceptor, threadArgs);
    if (err != 0) {
        removeUserThreadNodeNoblock(server, userNode);
        pthread_mutex_unlock(&server->user_list.lock);
        free(threadArgs);
        close(clientSocket);
        fprintf(stderr, "TCP server create thread fail: %s\n", strerror(err));
        return err;
    }
    pthread_mutex_unlock(&server->user_list.lock);
    return 0;
}

/**
 * terminate thread
 **/
void failServerThread(TCPServer *server, int err) {
    int lockErr;
    lockErr = pthread_mutex_lock(&server->lock);
    if (lockErr != 0) {
        fprintf(stderr, "TCP server can't lock mutex: %s\n", strerror(lockErr));
    } else {
        server->error = err;
        server->state = SERVER_FAIL;
        pthread_mutex_unlock(&server->lock);
    }
    //Not check errors
    //EINVAL the mutex has not been properly initialized, but this function already lock this mutex - impossible
    //EPERM  the calling thread does not own the mutex - impossible
    fprintf(stderr, "TCP server fail: %s\n", strerror(err));
    //Not check errors
    //EINVAL thread is not a joinable thread
    //ESRCH  No thread with the ID thread could be found.
    //TODO: stop user thread...
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

_Noreturn void *serverWorker(void *args) {
    struct ServerArgs *serverArgs = (struct ServerArgs *) args;
    TCPServer *server = serverArgs->server;
    AcceptFunc acceptFunc = serverArgs->func;
    free(args);

    int err;
    int lockErr;
    address clientAddress;
    err = listen(server->socket, LISTEN_QUEUE_SIZE);
    if (err != 0)
        failServerThread(server, err);
    while (1) {
        int clientSocket = accept(server->socket, &clientAddress.socket_address, &(server->address.address_len));
        if (clientSocket != -1) {
            err = addUserNode(server, clientAddress, clientSocket, acceptFunc);
            if (err != 0) {
                fprintf(stderr, "Fail to accept user connection: %s", strerror(err));
            }
        } else {
            failServerThread(server, clientSocket);
        }
    }
}

int startServer(TCPServer *server, AcceptFunc func) {
    int err;
    err = pthread_mutex_lock(&server->lock);
    if (err != 0)
        return err;
    int isBusy = server->state != SERVER_NOT_STARTED;
    if (isBusy) {
        //Not check errors
        //EINVAL the mutex has not been properly initialized, but this function already lock this mutex - impossible
        //EPERM  the calling thread does not own the mutex - impossible
        pthread_mutex_unlock(&server->lock);
        return EBUSY;
    }
    struct ServerArgs *args = malloc(sizeof(struct ServerArgs));
    args->server = server;
    args->func = func;
    err = pthread_create(&server->main_thread, NULL, serverWorker, args);
    if (err != 0) {
        free(args);
        //Not check errors
        //EINVAL the mutex has not been properly initialized, but this function already lock this mutex - impossible
        //EPERM  the calling thread does not own the mutex - impossible
        pthread_mutex_unlock(&server->lock);
        return err;
    }
    server->state = SERVER_ACTIVE;
    //Not check errors
    //EINVAL the mutex has not been properly initialized, but this function already lock this mutex - impossible
    //EPERM  the calling thread does not own the mutex - impossible
    pthread_mutex_unlock(&server->lock);
    return 0;
}
