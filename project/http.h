#pragma once
#define OK 0

struct httpRequest {
    char* method;
    char* uri;
    char* protocol;
    char* host;
    char* body;
};

int acceptRequest(int socket, struct httpRequest* data);