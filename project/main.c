#define _GNU_SOURCE

#include <stdio.h>
#include "tcp.h"
#include "http.h"
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#define SERVER_ADDRESS htonl(INADDR_ANY)
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

void send_status(int sfd, int code, char *desc) {
    char answer[1024];
    size_t toWrite;
    size_t wrote = 0;
    toWrite = snprintf(answer, 1024, "HTTP/1.1 %d %s\r\n\r\n", code, desc);
    if (toWrite == -1)
        return;
    while (wrote < toWrite) {
        size_t s = send(sfd, answer + wrote, toWrite - wrote, 0);
        wrote += s;
    }
}

HttpParameter *select_host(HttpRequest *request) {
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
    char *res;
    char addressString[255];
    int error = 0;
    enum AcceptStatus status;

    res = tpc_address_to_string(&con->ip_address.address, addressString, sizeof(addressString));

    if (res != NULL) {
        printf("[%u] Client address %s\n", gettid(), addressString);
    } else {
        fprintf(stderr,"[%u] Can't convert client address to string\n", gettid());
    }

    status = accept_request(con->socket, &request);
    switch (status) {
        case ACCEPT_OK:
            break;
        case ACCEPT_ERROR:
            fprintf(stderr, "[%u] Request parse error\n", gettid());
            send_status(con->socket, 400, "Wrong http request");
            return;
        case ACCEPT_SOCKET_CLOSE:
            fprintf(stderr, "[%u] Socket close in time of parser request\n", gettid());
            send_status(con->socket, 400, "Socket closed");
            return;
        case ACCEPT_NO_MEMORY:
            fprintf(stderr, "[%u] Not enough memory\n", gettid());
            send_status(con->socket, 500, "Internal error");
            return;
    }

    printf("[%u] %.*s %.*s %.*s\n",
           gettid(),
           (int) request.method_size, request.method,
           (int) request.uri_size, request.uri,
           (int) request.protocol_size, request.protocol);

#ifdef DEBUG
    write(STDOUT_FILENO, request.request, request.request_size);
    write(STDOUT_FILENO, "\n", 1);
#endif

    HttpParameter *host = select_host(&request);
    if (host == NULL) {
        deconstruct_request(&request);
        fprintf(stderr,"[%u] Can't found host in request, error\n", gettid());
        send_status(con->socket, 400, "Wrong http request, no host");
        return;
    }
    char addr[host->value_size + 1];
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
    //Select host ip
    IpAddress address;
    enum SelectStatus selectStatus;
    if (port == NULL) {
        selectStatus = tcp_select_addr(addr, 80, &address, &error);
    } else {
        selectStatus = tcp_select_addr_str_port(addr, port, &address, &error);
    }
    switch (selectStatus) {
        case SELECT_NO_RESULT:
            deconstruct_request(&request);
            fprintf(stderr, "[%u] Can't select host address\n", gettid());
            send_status(con->socket, 500, "Can't found host ip");
            return;
        case SELECT_ERROR:
            deconstruct_request(&request);
            fprintf(stderr, "[%u] Addr info error %s\n", gettid(), gai_strerror(error));
            send_status(con->socket, 500, "Internal error");
            return;
        case SELECT_OK:
            break;

    }
    //check for cycle
    if (address.address.sa_family == AF_INET) {
        struct sockaddr_in *inAddr = (struct sockaddr_in *) &address.address;
        if (inAddr->sin_addr.s_addr == SERVER_ADDRESS && inAddr->sin_port == SERVER_PORT) {
            deconstruct_request(&request);
            fprintf(stderr, "[%u] Find connection loop\n", gettid());
            send_status(con->socket, 508, "Connection loop");
            return;
        }
    }
    //print request
    res = tpc_address_to_string(&address.address, addressString, 255);
    if (res != NULL) {
        printf("[%u] Target server %s\n", gettid(), addressString);
    } else {
        fprintf(stderr, "[%u] Can't write address to string\n", gettid());
    }
    Connection *dst_con = tcp_create_connection(&address, &error);
    EchoStatus echoStatus;
    if (dst_con == NULL) {
        deconstruct_request(&request);
        fprintf(stderr, "[%u] Can't create server connection: %s\n", gettid(), strerror(error));
        send_status(con->socket, 500, "Can't connect to server");
        return;
    }
    pthread_cleanup_push((void (*)(void *)) tcp_deconstruct_connection, dst_con)
            int isHttpTunnel = 0;
            if (equals_string(request.method, request.method_size, "CONNECT", sizeof("CONNECT") - 1)) {
                isHttpTunnel = 1;
            }
            if (!isHttpTunnel) {
                size_t write_size = 0;
                while (write_size < request.request_size) {
                    size_t w = send(dst_con->socket, request.request + write_size, request.request_size - write_size,
                                    0);
                    if (w == -1) {
                        deconstruct_request(&request);
                        fprintf(stderr, "[%u] Can't send request to server %s\n", gettid(), strerror(errno));
                        send_status(con->socket, 502, "Bad host answer");
                        tcp_deconstruct_connection(dst_con);
                        return;
                    }
                    write_size += w;
                }
            } else {
                send_status(con->socket, 200, "OK");
            }
            deconstruct_request(&request);
            echoStatus = tcp_sockets_echo(dst_con->socket, con->socket, &error);
            switch (echoStatus) {
                case ECHO_OK:
                    break;
                case ECHO_POLL_ERROR:
                case ECHO_RECV_ERROR:
                case ECHO_SEND_ERROR:
                case ECHO_SOCKET_ERROR:
                    fprintf(stderr, "[%u] Echo error: %s\n", gettid(), strerror(error));
                    break;
                case ECHO_SOCKET_CLOSED:
                    printf("[%u] Socket closed\n", gettid());
                    break;
                case ECHO_SOCKET_WRONG:
                    fprintf(stderr, "[%u] Wrong socket\n", gettid());
                    break;
            }
    pthread_cleanup_pop(1);
}

void pipeAction(int sig, siginfo_t* info, void* ucontext) {
    char buf[4094];
    size_t _write = 0;
    int size = snprintf(buf, 4094, "[%d] handle SIGPIPE from %d\n", gettid(), info->si_pid);
    while (_write < size) {
        size_t w = write(STDERR_FILENO, buf, size - _write);
        if (w == -1)
            break;
        _write += w;
    }
}

int main() {
    //Create handler for sigpipe
    //He may end the process unexpectedly
    struct sigaction act;
    act.sa_sigaction = pipeAction;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGPIPE, &act, NULL);
    debug("Main: pid %d\n", getpid())
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = SERVER_PORT;
    addr.sin_addr.s_addr = SERVER_ADDRESS;
    TCPServer *server = tcp_create_server((struct sockaddr *) &addr, sizeof(addr));
    if (server == NULL) {
        fprintf(stderr, "Main: Server create error: %s\n", strerror(errno));
        return -1;
    }
    int err = tcp_start_server(server, acceptFunc);
    if (err != 0) {
        fprintf(stderr, "Main: Server start error: %s\n", strerror(err));
        return -1;
    }
    printf("Main: start http proxy server\n");
    char c = 0;
    size_t readSize;
    while (c != 'c') {
       readSize = read(STDIN_FILENO, &c, 1);
       if (readSize == -1)
           break;
    }
    if (readSize == -1) {
        fprintf(stderr, "Main: Command listener loop error %s\n", strerror(errno));
    }
    int serverError = 0;
    int error;
    error = tcp_deconstruct_server(server, &serverError);
    if (error != 0)
        printf("Main: Server deconstruct error: %s\n", strerror(error));
    if (serverError != 0)
        printf("Main: Server internal error: %s\n", strerror(serverError));
    puts("Main: Terminate!\n");
    return 0;
}
