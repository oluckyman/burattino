#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"

static const char *TAG = "bur[http]";


void send_header(struct netconn *conn, int http_code) {
    char *header;
    switch (http_code) {
        case 200:
            header = "HTTP/1.1 200 OK\r\nContent-type: application/json\r\n\r\n";
            break;
        case 400:
            header = "HTTP/1.1 400 Bad Request\r\nContent-type: application/json\r\n\r\n";
            break;
        case 404:
            header = "HTTP/1.1 404 Not Found\r\n\r\n";
            break;
        default:
            header = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    }
    printf(header);
    netconn_write(conn, header, strlen(header), NETCONN_COPY);
}


void send_body(struct netconn *conn, int http_code, char *message) {
    char *status = "";
    switch (http_code) {
        case 200:
            status = "OK";
            break;
        default:
            if (strlen(message)) {
                status = "Error";
            }
    }

    if (strlen(status)) {
        size_t body_max_size = strlen(status) + strlen(message) + 200;
        char body[body_max_size];

        int cursor = sprintf(body, "{");

        cursor += sprintf(body + cursor, "\"status\":\"%s\"", status);

        if (strlen(message)) {
            cursor += sprintf(body + cursor, ",");
            cursor += sprintf(body + cursor, "\"message\":\"%s\"", message);
        }

        sprintf(body + cursor, "}");

        printf("%s\n", body);
        netconn_write(conn, body, strlen(body), NETCONN_COPY);
    }
}


void response(struct netconn *conn, int http_code, char *message) {
    ESP_LOGI(TAG, "<<<<<<<<<< Response:");

    send_header(conn, http_code);
    send_body(conn, http_code, message);
    printf("==========\n");
}


void handle_request(struct netconn *conn, char *request) {
    char *endpoint = "GET /register";
    if (strncmp(endpoint, request, strlen(endpoint)) != 0) {
        response(conn, 404, "");
        return;
    }

    int plant_id;
    int parseResult = sscanf(request + strlen(endpoint), "?plant_id=%d", &plant_id);
    if (parseResult != 1) {
        char message[] = "Parameter `plant_id` is required";
        response(conn, 400, message);
        return;
    }

    // TODO: send request to backend here

    // Temporary success response
    //
    char *format = "Got plant ID: %d";
    size_t length = snprintf(NULL, 0, format, plant_id);
    char message[length];
    snprintf(message, length + 1, format, plant_id);

    response(conn, 200, message);

    ESP_LOGI(TAG, "OK");
}


static void http_server_netconn_serve(struct netconn *conn) {
    struct netbuf *inbuf;
    char *buf;
    u16_t buflen;
    err_t err;

    /* Read the data from the port, blocking if nothing yet there.
       We assume the request (the part we care about) is in one netbuf */
    err = netconn_recv(conn, &inbuf);

    ESP_LOGI(TAG, ">>>>>>>>>> Request:");
    if (err == ERR_OK) {
        netbuf_data(inbuf, (void**)&buf, &buflen);
        printf("%.*s\n----------\n", buflen, buf);

        handle_request(conn, buf);
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
