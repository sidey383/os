#include "socketEcho.h"
#include <poll.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>

struct PollAction {
    enum EchoStatus (*func)(int, int, int*);
    short int flag;
};

enum EchoStatus tcp_echo_packet(int in, int out, int *error) {
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
        sendSize+= s;
    }
    return ECHO_OK;
}

enum EchoStatus tcp_socket_error(int in, int out, int *error) {
    socklen_t len = sizeof(int);
    int getError = getsockopt(in, SOL_SOCKET, SO_ERROR, error, &len);
    if (getError != 0) {
        (*error) = getError;
    }
    return ECHO_SOCKET_ERROR;
}

enum EchoStatus tcp_socket_close(int in, int out, int* error) {
    return ECHO_SOCKET_CLOSED;
}

enum EchoStatus tcp_socket_wrong(int in, int out, int* error) {
    return ECHO_SOCKET_WRONG;
}

enum EchoStatus tcp_sockets_echo(int socket1, int socket2, int *error) {
    struct PollAction actions[4] = {
            {tcp_echo_packet, POLLIN | POLLPRI},
            {tcp_socket_close, POLLHUP | POLLRDHUP},
            {tcp_socket_wrong, POLLNVAL},
            {tcp_socket_error, POLLERR}
    };

    struct pollfd polls[2] = {
            {socket1, POLLIN, 0},
            {socket2, POLLIN, 0}
    };
    int isWork = 1;
    int status;
    int err;
    enum EchoStatus result;
    while (isWork) {
        status = poll(polls, 2, POLL_TIMEOUT);
        if (status == -1)
            return ECHO_POLL_ERROR;
        for (int i = 0; i < 2; i++) {
            struct pollfd* in = polls + i;
            struct pollfd* out = polls + ((i + 1)%2);
            for (int p = 0; p < sizeof(actions)/sizeof(actions[0]); p++) {
                if ((in->revents & actions[p].flag) != 0) {
                    in->revents = (short) (in->revents & (~actions[p].flag));
                    result = actions[p].func(in->fd, out->fd, error);
                    if (result != ECHO_OK)
                        return result;
                }
            }
            if (in->revents != 0) {
                fprintf(stderr, "Not all poll event were intercepted: %d\n", in->revents);
                in->revents = 0;
            }
        }
    }
}

int tcp_select_addr(const char *address, uint16_t port, struct ipaddres* result, int *error) {
    struct ipaddres result{};
    struct addrinfo *addrInfoList;
    struct addrinfo hints{};
    char portStr[16];
    int err;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;
    err = getaddrinfo(address, portStr, &hints, &addrInfoList);
    if (err != 0) {
        (*error) = err;
        return 0;
    }

}
