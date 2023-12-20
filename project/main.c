#include <stdio.h>
#include "tcp.h"
#include "http.h"
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SERVER_ADDRESS htonl(INADDR_LOOPBACK)
#define SERVER_PORT htons(8080)

static int equals_string(const char *str1, size_t size1, const char *str2, size_t size2) {
    if (size1 != size2) {
        return 0;
    }
    for (size_t i = 0; i < size1; i++) {
        if (str1[i] != str2[i])
            return 0;
    }
    return 1;
}

void send_status(int sfd, int code) {
    char answer[1024];
    size_t toWrite;
    size_t wrote = 0;
    toWrite = snprintf(answer, 1024, "HTTP/1.1 %d Error\r\n\r\n", code);
    if (toWrite == -1)
        return;
    while (wrote < toWrite) {
        size_t s = send(sfd, answer + wrote, toWrite - wrote, 0);
        wrote += s;
    }
}

HttpParameter *select_host(HttpRequest* request) {
    HttpParameter *host = NULL;
    for (HttpParameter *p = request->parameters; p != NULL; p = p->next) {
        if (equals_string(p->name, p->name_size, "Host", 4) ||
            (equals_string(p->name, p->name_size, "host", 4) && host == NULL)
                ) {
            host = p;
        }
    }
    return host;
}


void acceptFunc(Connection *con) {
    HttpRequest request;
    enum AcceptStatus status = accept_request(con->socket, &request);
    switch (status) {
        case ACCEPT_OK:
            break;
        case ACCEPT_ERROR:
            puts("Request parse error\n");
            send_status(con->socket, 400);
            return;
        case ACCEPT_SOCKET_CLOSE:
            puts("Socket close in time of parser request\n");
            send_status(con->socket, 400);
            return;
    }

    HttpParameter *host = select_host(&request);
#ifdef DEBUG
    write(STDOUT_FILENO, request.request, request.request_size);
    write(STDOUT_FILENO, "\n", 1);
#endif
    if (host == NULL) {
        send_status(con->socket, 400);
        return;
    }
    char addr[host->value_size + 1];
    int error;
    memcpy(addr, host->value, host->value_size);
    addr[host->value_size] = 0;
    char *port = NULL;
    // found port if contain
    for (size_t i = host->value_size; i > 0; i--) {
        if (addr[i] == ':') {
            addr[i] = 0;
            port = addr + i + 1;
            break;
        }
    }
    IpAddress address;
    enum SelectStatus selectStatus;
    if (port == NULL) {
        selectStatus = tcp_select_addr(addr, 80, &address, &error);
    } else {
        selectStatus = tcp_select_addr_str_port(addr, port, &address, &error);
    }
    switch (selectStatus) {
        case SELECT_NO_RESULT:
            fputs("No result for this address\n", stderr);
            send_status(con->socket, 500);
            return;
        case SELECT_ERROR:
            fprintf(stderr, "Addr info error %s\n", gai_strerror(error));
            send_status(con->socket, 500);
            return;
        case SELECT_OK:
            break;

    }
    //check for cycle
    if (address.address.sa_family == AF_INET) {
        struct sockaddr_in *inAddr = (struct sockaddr_in *) &address.address;
        if (inAddr->sin_addr.s_addr == SERVER_ADDRESS && inAddr->sin_port == SERVER_PORT) {
            send_status(con->socket, 508);
            return;
        }
    }
    //print request
    char addressString[255];
    char *res = tpc_address_to_string(&address.address, addressString, 255);
    if (res == NULL) {
        fprintf(stderr, "Can't write address to string\n");
    } else {
        printf("Connect %s\n", addressString);
        printf("%.*s %.*s %.*s\n",
               (int) request.method_size, request.method,
               (int) request.uri_size, request.uri,
               (int) request.protocol_size, request.protocol);
    }
    Connection *dst_con = tcp_create_connection(&address, &error);
    EchoStatus echoStatus;
    pthread_cleanup_push((void (*)(void *)) tcp_deconstruct_connection, dst_con)
            if (dst_con == NULL) {
                fprintf(stderr, "Can't create client connection %s\n", strerror(error));
                send_status(con->socket, 500);
                return;
            }
            size_t write_size = 0;
            while (write_size < request.request_size) {
                size_t w = send(dst_con->socket, request.request + write_size, request.request_size - write_size, 0);
                if (w == -1) {
                    fprintf(stderr, "Can't send message to destination server %s\n", strerror(errno));
                    send_status(con->socket, 502);
                    return;
                }
                write_size += w;
            }
            echoStatus = tcp_sockets_echo(dst_con->socket, con->socket, &error);
            switch (echoStatus) {
                case ECHO_OK:
                    break;
                case ECHO_POLL_ERROR:
                case ECHO_RECV_ERROR:
                case ECHO_SEND_ERROR:
                case ECHO_SOCKET_ERROR:
                    fprintf(stderr, "Echo error: %s\n", strerror(error));
                    break;
                case ECHO_SOCKET_CLOSED:
                    puts("Socket closed!");
                    break;
                case ECHO_SOCKET_WRONG:
                    fputs("Wrong socket!\n", stderr);
                    break;
            }
    pthread_cleanup_pop(1);
}

int main() {
    debug("pid %d\n", getpid())
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = SERVER_PORT;
    addr.sin_addr.s_addr = SERVER_ADDRESS;
    TCPServer *server = tcp_create_server((struct sockaddr *) &addr, sizeof(addr));
    if (server == NULL) {
        fprintf(stderr, "Server create error: %s\n", strerror(errno));
        return -1;
    }
    int err = tcp_start_server(server, acceptFunc);
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
    error = tcp_deconstruct_server(server, &serverError);
    if (error != 0)
        printf("Main: server deconstruct error: %s", strerror(error));
    if (serverError != 0)
        printf("Main: server internal error: %s", strerror(serverError));
    puts("Terminate!\n");
    return 0;
}
