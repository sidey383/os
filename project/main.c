#include <stdio.h>
#include "tcp.h"
#include "http.h"
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>


void acceptFunc(ClientConnection *con) {
    HttpRequest request;
    enum AcceptStatus status = accept_request(con->socket, &request);
    switch (status) {
        case ACCEPT_OK:
            printf("Success parser request\n");
            break;
        case ACCEPT_ERROR:
            printf("Request parse error\n");
            return;
        case ACCEPT_SOCKET_CLOSE:
            printf("Socket close in time of parser request\n");
            return;
    }
    write(STDOUT_FILENO, request.method, request.method_size);
    write(STDOUT_FILENO, " ", 1);
    write(STDOUT_FILENO, request.uri, request.uri_size);
    write(STDOUT_FILENO, " ", 1);
    write(STDOUT_FILENO, request.protocol, request.protocol_size);
    write(STDOUT_FILENO, "\n", 1);
    for (HttpParameter *p = request.parameters; p != NULL; p = p->next) {
        write(STDOUT_FILENO, p->name, p->name_size);
        write(STDOUT_FILENO, ": ", 2);
        write(STDOUT_FILENO, p->value, p->value_size);
        write(STDOUT_FILENO, "\n", 1);
    }
    //TODO: create server socket and enable echo server
}

int main() {
    debug("pid %d\n", getpid())
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    TCPServer *server = create_server((struct sockaddr *) &addr, sizeof(addr));
    if (server == NULL) {
        fprintf(stderr, "Server create error: %s\n", strerror(errno));
        return -1;
    }
    int err = start_server(server, acceptFunc);
    if (server == NULL) {
        fprintf(stderr, "Server start error: %s\n", strerror(err));
        return -1;
    }
    char c = 0;
    while (c != 'c') {
        scanf("%c", &c);
    }
    int serverError = 0;
    int error = 0;
    error = deconstruct_server(server, &serverError);
    if (error != 0)
        printf("Main: server deconstruct error: %s", strerror(error));
    if (serverError != 0)
        printf("Main: server internal error: %s", strerror(serverError));
    puts("Terminate!\n");
    return 0;
}
