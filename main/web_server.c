#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"

static const char *TAG = "bur[http]";


static void http_server_netconn_serve(struct netconn *conn) {
    struct netbuf *inbuf;
    char *buf;
    u16_t buflen;
    err_t err;

    /* Read the data from the port, blocking if nothing yet there.
       We assume the request (the part we care about) is in one netbuf */
    err = netconn_recv(conn, &inbuf);

    char http_200_header[] = "HTTP/1.1 200 OK\r\nContent-type: application/json\r\n\r\n";
    char http_404_header[] = "HTTP/1.1 404 Not Found\r\nContent-type: application/json\r\n\r\n";
    char http_400_header[] = "HTTP/1.1 400 Bad Request\r\nContent-type: application/json\r\n\r\n";

    ESP_LOGI(TAG, "Got connection");
    if (err == ERR_OK) {
        netbuf_data(inbuf, (void**)&buf, &buflen);

        printf("> > >\n%.*s\n< < <\n", buflen, buf);

        char *endpoint = "GET /register";
        if (strncmp(endpoint, buf, strlen(endpoint)) == 0) {
            // TODO: send request to backend here

            int plantId;
            if (sscanf(buf + strlen(endpoint), "?plant_id=%d", &plantId) == 1) {
                printf(" PlantId: %d\n\n", plantId);
                netconn_write(conn, http_200_header, sizeof(http_200_header)-1, NETCONN_NOCOPY);
                char http_index_html[] = "{\"status\": \"OK\"}";
                netconn_write(conn, http_index_html, sizeof(http_index_html)-1, NETCONN_NOCOPY);
                ESP_LOGI(TAG, "OK");
            } else {
                netconn_write(conn, http_400_header, sizeof(http_400_header)-1, NETCONN_NOCOPY);
                char http_index_html[] = "{\"status\": \"Error\",\"message\":\"`plant_id` is required\"}";
                netconn_write(conn, http_index_html, sizeof(http_index_html)-1, NETCONN_NOCOPY);
            }

        } else {
            netconn_write(conn, http_404_header, sizeof(http_404_header)-1, NETCONN_NOCOPY);
            ESP_LOGI(TAG, "Not found");
        }
    }
    /* Close the connection (server closes in HTTP) */
    netconn_close(conn);
    ESP_LOGI(TAG, "Close connection");

    /* Delete the buffer (netconn_recv gives us ownership,
       so we have to make sure to deallocate the buffer) */
    netbuf_delete(inbuf);
}

static void http_server_task(void *pvParameters) {
    struct netconn *conn, *newconn;
    err_t err;
    conn = netconn_new(NETCONN_TCP);
    netconn_bind(conn, NULL, 80);
    netconn_listen(conn);
    ESP_LOGI(TAG, "Start webserver");
    do {
        err = netconn_accept(conn, &newconn);
        if (err == ERR_OK) {
            http_server_netconn_serve(newconn);
            netconn_delete(newconn);
        }
    } while(err == ERR_OK);
    netconn_close(conn);
    netconn_delete(conn);
}


void initialize_web_server() {
    xTaskCreate(&http_server_task, "http_server_task", 2048, NULL, 5, NULL);
}
