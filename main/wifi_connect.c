#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_smartconfig.h"
#include "esp_wifi.h"
#include "tcpip_adapter.h"
#include "wifi_connect.h"

static const bool TEST_MODE = false;

static EventGroupHandle_t event_group;
static const int CONNECTED_BIT = BIT0;
static const char *TAG = "bur[wifi]";
void smartconfig_task(void * parm);

static esp_err_t event_handler(void *ctx, system_event_t *event) {
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
            if (TEST_MODE) {
                esp_wifi_connect();
            } else {
                xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
            }
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
            if (TEST_MODE) {
                xEventGroupSetBits(event_group, CONNECTED_BIT);
                xEventGroupSetBits(event_group, WIFI_SETUP_DONE_BIT);
            } else {
                xEventGroupSetBits(event_group, CONNECTED_BIT);
            }
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
            esp_wifi_connect();
            xEventGroupClearBits(event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}


static void sc_callback(smartconfig_status_t status, void *pdata) {
    switch (status) {
        case SC_STATUS_WAIT:
            ESP_LOGI(TAG, "SC_STATUS_WAIT");
            break;
        case SC_STATUS_FIND_CHANNEL:
            ESP_LOGI(TAG, "SC_STATUS_FINDING_CHANNEL");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            ESP_LOGI(TAG, "SC_STATUS_GETTING_SSID_PSWD");
            break;
        case SC_STATUS_LINK:
            ESP_LOGI(TAG, "SC_STATUS_LINK");
            wifi_config_t *wifi_config = pdata;
            ESP_LOGI(TAG, "SSID:%s", wifi_config->sta.ssid);
            ESP_LOGI(TAG, "PASSWORD:%s", wifi_config->sta.password);
            ESP_ERROR_CHECK( esp_wifi_disconnect() );
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );
            break;
        case SC_STATUS_LINK_OVER:
            ESP_LOGI(TAG, "SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                uint8_t device_ip[4] = { 0 };
                memcpy(device_ip, (uint8_t* )pdata, 4);
                ESP_LOGI(TAG, "My IP: %d.%d.%d.%d\n", device_ip[0], device_ip[1], device_ip[2], device_ip[3]);
            }
            xEventGroupSetBits(event_group, WIFI_SETUP_DONE_BIT);
            break;
        default:
            break;
    }
}


void smartconfig_task(void * parm) {
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    ESP_ERROR_CHECK( esp_smartconfig_start(sc_callback) );
    while (1) {
        uxBits = xEventGroupWaitBits(event_group, CONNECTED_BIT | WIFI_SETUP_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to AP");
        }
        if(uxBits & WIFI_SETUP_DONE_BIT) {
            ESP_LOGI(TAG, "WiFi setup is over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}


void initialize_wifi(EventGroupHandle_t _event_group) {
    tcpip_adapter_init();
    event_group = _event_group;

    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );

    // no smartconfig
    wifi_config_t sta_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .bssid_set = false
        }
    };
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
    //

    ESP_ERROR_CHECK( esp_wifi_start() );
}
