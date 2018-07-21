#include "freertos/FreeRTOS.h"
/* #include "esp_wifi.h" */
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "wifi_connect.h"
#include "web_server.h"

/* FreeRTOS event group to signal when we are connected and ready to use wifi */
static EventGroupHandle_t event_group;

static const char *TAG = "bur";

void app_main()
{
    ESP_LOGI(TAG, "\n\n\n====== Поехали! ======\n\n");
    ESP_ERROR_CHECK( nvs_flash_init() );

    event_group = xEventGroupCreate();

    initialize_wifi(event_group);
    xEventGroupWaitBits(event_group, WIFI_SETUP_DONE_BIT, true, false, portMAX_DELAY);
    initialize_web_server();
}
