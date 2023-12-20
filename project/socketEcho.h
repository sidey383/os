#pragma once
#include <sys/socket.h>

#define POLL_TIMEOUT 200
#define BUFFER_SIZE 32000

enum EchoStatus {
    ECHO_OK,
    ECHO_SOCKET_CLOSED,
    ECHO_SOCKET_ERROR,
    ECHO_SOCKET_WRONG,
    ECHO_RECV_ERROR,
    ECHO_SEND_ERROR,
    ECHO_POLL_ERROR
};

struct IpAddress {
    socklen_t addrlen;
    struct sockaddr addr;
};

enum EchoStatus tcp_sockets_echo(int socket1, int socket2, int *error);

int tcp_select_addr(const char *address, uint16_t port, struct ipaddres* result, int *error);
