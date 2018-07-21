#ifndef WIFI_CONNECT_H_INCLUDED
#define WIFI_CONNECT_H_INCLUDED

static const int WIFI_SETUP_DONE_BIT = BIT1;

void initialize_wifi(EventGroupHandle_t _event_group);

#endif
