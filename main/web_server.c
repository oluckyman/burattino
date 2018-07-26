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


void handle_request(struct netconn *conn, const char *request) {
    const char *endpoint = "GET /register";
    if (strncmp(endpoint, request, strlen(endpoint)) != 0) {
        response(conn, 404, "");
        return;
    }

    int plant_id;
    const size_t token_size = 1000;
    char *token = malloc(token_size + 1);
    int parseResult = parseRequest(request + strlen(endpoint), &plant_id, token, token_size);
    ESP_LOGI(TAG, "scanned: %d, plant_id: %d, token: %s", parseResult, plant_id, token);
    if (parseResult != 2) {
        const char message[] = "Parameters `plant_id` and `token` are required";
        response(conn, 400, message);
        return;
    }

    /* http_request(); */

    // Temporary success response
    //
    const char *format = "Got plant ID: %d and token: %.10s...";
    size_t length = snprintf(NULL, 0, format, plant_id, token);
    char message[length + 1];
    snprintf(message, length, format, plant_id, token);

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


#define WEB_SERVER "buratino.asobolev.ru"
#define WEB_URL "/api/v1/plants/52/"
#define TOKEN "Token eyJhbGciOiJSUzI1NiIsImtpZCI6ImI4OWY3MzQ2YTA5ODVmNDIxZGNkOGQzMGMwYjMwZWViZmFlMTlhMWUifQ.eyJpc3MiOiJodHRwczovL3NlY3VyZXRva2VuLmdvb2dsZS5jb20vYnVyYXR0aW5vLTFkYzI0IiwibmFtZSI6IklseWEgIiwiYXVkIjoiYnVyYXR0aW5vLTFkYzI0IiwiYXV0aF90aW1lIjoxNTI5MDY5MzEzLCJ1c2VyX2lkIjoiYzAwbWMwbGxiWFBCekI0NjJmVFBGM1d2SzcxMiIsInN1YiI6ImMwMG1jMGxsYlhQQnpCNDYyZlRQRjNXdks3MTIiLCJpYXQiOjE1MzI0NjM3MTYsImV4cCI6MTUzMjQ2NzMxNiwiZW1haWwiOiJidXJhdHRpbm9AYmVsc2t5LmluIiwiZW1haWxfdmVyaWZpZWQiOmZhbHNlLCJmaXJlYmFzZSI6eyJpZGVudGl0aWVzIjp7ImVtYWlsIjpbImJ1cmF0dGlub0BiZWxza3kuaW4iXX0sInNpZ25faW5fcHJvdmlkZXIiOiJwYXNzd29yZCJ9fQ.Tb1OTv777v-mHMYVIl8TKFbDVdyDnNFMa5KQ7gNDUT4QYX0XrfvEH_4Y0nmRPuAUuRsfjj1q5956eUu5dZ8EyBYdnHr9GBQjUSvV0HcIZW6U_LBCt35PdPzIazkVDbHQ6egT7xu3YHcoCUvOtdvOQHvzagMKbJ4cLc881jHTBe6FiVbfR3uiJVLk5w6jZZLr-IC5b0yd40GMhcnLVQE1EOW1vQeEgzLlYpvVN_NRgoo6tgCx0--URqfp1DtwxBsSOxIkEbX1W6tGCK6BuAIi0r4PBm2fHaAUgGreTJMdDaIxjiXYMgiF65O7CISFF54-bRQKkYAntbkU0DR6OOyRXw"
#define DEVICE_ID "28f57ad5-a6ec-482f-a396-92b5cabbf211"


void initialize_web_server(EventGroupHandle_t _event_group) {
    event_group = _event_group;
    xTaskCreate(&http_server_task, "http_server_task", 1024 * 4, NULL, 5, NULL);
    /* ESP_LOGI(TAG, ">>>> Start Request"); */
    /* RequestParams params = { */
    /*     .host = WEB_SERVER, */
    /*     .url = WEB_URL, */
    /*     .token = TOKEN, */
    /*     .body = "{\"name\":\"esp-32\", \"plant_type\": 7}", */
    /* }; */
    /* http_request(event_group, &params); */
    /* ESP_LOGI(TAG, "<<<< Finish Request"); */
}
