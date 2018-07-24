#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "./http_request.h"

#define WEB_SERVER "buratino.asobolev.ru"
#define WEB_PORT 80
#define WEB_URL "/api/v1/plants/52/"


static EventGroupHandle_t event_group;

static const char *TAG = "bur[http-request]";

#define TOKEN "Token eyJhbGciOiJSUzI1NiIsImtpZCI6ImI4OWY3MzQ2YTA5ODVmNDIxZGNkOGQzMGMwYjMwZWViZmFlMTlhMWUifQ.eyJpc3MiOiJodHRwczovL3NlY3VyZXRva2VuLmdvb2dsZS5jb20vYnVyYXR0aW5vLTFkYzI0IiwibmFtZSI6IklseWEgIiwiYXVkIjoiYnVyYXR0aW5vLTFkYzI0IiwiYXV0aF90aW1lIjoxNTI5MDY5MzEzLCJ1c2VyX2lkIjoiYzAwbWMwbGxiWFBCekI0NjJmVFBGM1d2SzcxMiIsInN1YiI6ImMwMG1jMGxsYlhQQnpCNDYyZlRQRjNXdks3MTIiLCJpYXQiOjE1MzI0NjM3MTYsImV4cCI6MTUzMjQ2NzMxNiwiZW1haWwiOiJidXJhdHRpbm9AYmVsc2t5LmluIiwiZW1haWxfdmVyaWZpZWQiOmZhbHNlLCJmaXJlYmFzZSI6eyJpZGVudGl0aWVzIjp7ImVtYWlsIjpbImJ1cmF0dGlub0BiZWxza3kuaW4iXX0sInNpZ25faW5fcHJvdmlkZXIiOiJwYXNzd29yZCJ9fQ.Tb1OTv777v-mHMYVIl8TKFbDVdyDnNFMa5KQ7gNDUT4QYX0XrfvEH_4Y0nmRPuAUuRsfjj1q5956eUu5dZ8EyBYdnHr9GBQjUSvV0HcIZW6U_LBCt35PdPzIazkVDbHQ6egT7xu3YHcoCUvOtdvOQHvzagMKbJ4cLc881jHTBe6FiVbfR3uiJVLk5w6jZZLr-IC5b0yd40GMhcnLVQE1EOW1vQeEgzLlYpvVN_NRgoo6tgCx0--URqfp1DtwxBsSOxIkEbX1W6tGCK6BuAIi0r4PBm2fHaAUgGreTJMdDaIxjiXYMgiF65O7CISFF54-bRQKkYAntbkU0DR6OOyRXw"
#define DEVICE_ID "28f57ad5-a6ec-482f-a396-92b5cabbf211"


static const char *REQUEST = "PUT " WEB_URL " HTTP/1.0\r\n"
    "Host: "WEB_SERVER"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 34\r\n"
    "Authorization: "TOKEN"\r\n"
    "\r\n"
    "{\"name\":\"esp-32\", \"plant_type\": 7}";


void http_request_task(void *pvParameters) {
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    while(1) {
        int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* Code to print the resolved IP.
           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        ESP_LOGI(TAG, "... sending request:");
        printf(REQUEST);
        printf("\r\n\r\n");

        if (write(s, REQUEST, strlen(REQUEST)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout, sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) {
                putchar(recv_buf[i]);
            }
        } while(r > 0);

        printf("\r\n\r\n");
        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
        close(s);
        break;
    }
    xEventGroupSetBits(event_group, HTTP_REQUEST_DONE_BIT);
    vTaskDelete(NULL);
}


void http_request(EventGroupHandle_t _event_group) {
    event_group = _event_group;

    xEventGroupClearBits(event_group, HTTP_REQUEST_DONE_BIT);
    xTaskCreate(&http_request_task, "http_request_task", 4096, NULL, 5, NULL);
    xEventGroupWaitBits(event_group, HTTP_REQUEST_DONE_BIT, true, false, portMAX_DELAY);
}
