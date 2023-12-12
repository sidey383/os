#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include "http.h"

int acceptRequest(int socket, struct httpRequest* data) {
    struct msghdr msg;
    ssize_t status = recvmsg(socket, &msg, 0);
    if (status == -1) {
        return errno;
    }
    return OK;
}
