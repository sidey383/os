#include <stdio.h>
#include "tcp.h"
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

char buffer1[64];
char buffer2[256];
char buffer3[1024];
char buffer4[8192];

void acceptFunc(ClientConnection con) {
    struct msghdr msg;
    struct iovec iovecArray[] = {
            {buffer1, sizeof(buffer1)},
            {buffer2, sizeof(buffer2)},
            {buffer3, sizeof(buffer3)},
            {buffer4, sizeof(buffer4)}
    };
    msg.msg_iovlen = 4;
    msg.msg_iov = iovecArray;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;
    ssize_t size = recvmsg(con.socket, &msg, 0);
    if (size == -1) {
        fprintf(stderr, "Server work error: %s\n", strerror(errno));
        return;
    }
    printf("Accept %zu messages!\n", msg.msg_iovlen);
    for (size_t i = 0; i < msg.msg_iovlen; i++) {
        printf("%zu: %zu\n", i, msg.msg_iov[i].iov_len);
        write(STDOUT_FILENO, msg.msg_iov[i].iov_base, msg.msg_iov[i].iov_len);
        write(STDOUT_FILENO, "\n", 1);
    }
}

int main() {
    printf("pid %d\n", getpid());
    address addrs;
    addrs.address_len = sizeof(struct sockaddr_in);
    addrs.family = AF_INET;
    struct sockaddr_in *addr = (struct sockaddr_in *) &addrs.socket_address;
    addr->sin_family = AF_INET;
    addr->sin_port = htons(8080);
    addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TCPServer *server = createServer(&addrs);
    if (server == NULL) {
        fprintf(stderr, "Server create error: %s\n", strerror(errno));
        return -1;
    }
    int err = startServer(server, acceptFunc);
    if (server == NULL) {
        fprintf(stderr, "Server start error: %s\n", strerror(err));
        return -1;
    }
    pthread_exit(NULL);
}
