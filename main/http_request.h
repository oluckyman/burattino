#ifndef HTTP_REQUEST_H_INCLUDED
#define HTTP_REQUEST_H_INCLUDED

static const int HTTP_REQUEST_DONE_BIT = BIT2;

void http_request(EventGroupHandle_t _event_group);

#endif


