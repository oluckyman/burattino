#ifndef HTTP_REQUEST_H_INCLUDED
#define HTTP_REQUEST_H_INCLUDED

static const int HTTP_REQUEST_DONE_BIT = BIT2;

typedef struct RequestParams {
    char url[200];
    char token[1000];
    char body[200];
} RequestParams;

void http_request(EventGroupHandle_t _event_group, RequestParams *params);

#endif
