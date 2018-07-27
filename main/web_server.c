#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"
#include "./http_request.h"

static const char *TAG = "bur[http-server]";

static EventGroupHandle_t event_group;


void send_header(struct netconn *conn, int http_code) {
    const char *header;
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


void send_body(struct netconn *conn, int http_code, const char *message) {
    const char *status = "";
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
        char *body = malloc(body_max_size + 1);
        memset(body, 0, sizeof(*body));

        int cursor = sprintf(body, "{");

        cursor += sprintf(body + cursor, "\"status\":\"%s\"", status);

        if (strlen(message)) {
            cursor += sprintf(body + cursor, ",");
            cursor += sprintf(body + cursor, "\"message\":\"%s\"", message);
        }

        sprintf(body + cursor, "}");

        printf("%s\n", body);
        netconn_write(conn, body, strlen(body), NETCONN_COPY);
        free(body);
    }
}


void response(struct netconn *conn, int http_code, const char *message) {
    ESP_LOGI(TAG, "<<<<<<<<<< Response:");

    ESP_LOGI(TAG, "... sending header");
    send_header(conn, http_code);
    ESP_LOGI(TAG, "... header sent");
    ESP_LOGI(TAG, "... sending body");
    send_body(conn, http_code, message);
    ESP_LOGI(TAG, "... body sent");
    printf("==========\n");
}


int parseRequest(const char *input, int *plant_id, char *token_buf, size_t token_buf_size) {
    if (token_buf_size == 0) return 0;
    char format[21 + 5 + 1];
    snprintf(format, sizeof(format), "?plant_id=%%d&token=%%%ds", token_buf_size);
    return sscanf(input, format, plant_id, token_buf);
}


#define WEB_SERVER "buratino.asobolev.ru"
#define WEB_URL "http://" WEB_SERVER "/api/v1/plants/52/"
#define DEVICE_ID "28f57ad5-a6ec-482f-a396-92b5cabbf211"


void buildRequestParams(RequestParams *params, int plant_id, const char *token) {
    /* params = (RequestParams) { */
    /*     .host = WEB_SERVER, */
    /*     .url = WEB_URL, */
    /*     .token = token, */
    /*     .body = "{\"name\":\"esp-32\", \"plant_type\": 7}", */
    /* }; */

    /* strcpy(params->host, WEB_SERVER); */
    strcpy(params->url, WEB_URL);
    strcpy(params->token, token);
    char body[] = "{\"name\":\"esp-32\", \"plant_type\": 7}";
    strcpy(params->body, body);
}


void handle_request(struct netconn *conn, const char *request) {
    const char *endpoint = "GET /register";
    if (strncmp(endpoint, request, strlen(endpoint)) != 0) {
        response(conn, 404, "");
        return;
    }

    int plant_id;
    const size_t token_size = 1000;
    char *token = malloc(token_size + 1);
    memset(token, 0, sizeof(*token));
    int parsed = parseRequest(request + strlen(endpoint), &plant_id, token, token_size);

    ESP_LOGI(TAG, "parsed: %d, plant_id: %d, token: %.4s…%s", parsed, plant_id, token, token + strlen(token)-4);
    if (parsed != 2) {
        const char message[] = "Parameters `plant_id` and `token` are required";
        response(conn, 400, message);
        free(token);
        return;
    }

    ESP_LOGI(TAG, ">>>> Send Request to Backend");
    RequestParams *params = malloc(sizeof(RequestParams));
    memset(params, 0, sizeof(*params));
    buildRequestParams(params, plant_id, token);
    http_request(event_group, params);
    ESP_LOGI(TAG, "<<<< Finish Request to Backend");

    // Temporary success response
    //
    const char *format = "Got plant ID: %d and token: %.10s...";
    size_t message_size = snprintf(NULL, 0, format, plant_id, token);
    char message[message_size + 1];
    snprintf(message, message_size, format, plant_id, token);

    response(conn, 200, message);
    free(token);

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

    ESP_LOGI(TAG, ">>>>>>>>>> Incoming request:");
    if (err == ERR_OK) {
        netbuf_data(inbuf, (void**)&buf, &buflen);
        printf("%.*s\n", buflen, buf);
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


void initialize_web_server(EventGroupHandle_t _event_group) {
    event_group = _event_group;
    xTaskCreate(&http_server_task, "http_server_task", 1024 * 4, NULL, 5, NULL);
}
