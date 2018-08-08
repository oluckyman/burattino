#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"
#include "http_parser.h"
#include "./http_request.h"

static const char *TAG = "bur[http-server]";

static EventGroupHandle_t event_group;

enum parser_status {
    PARSER_STATUS_OK,
    PARSER_STATUS_PARSE_URL_ERROR,
    PARSER_STATUS_NOT_FOUND,
    PARSER_STATUS_INVALID_PARAMS,
};

typedef struct RegisterDeviceRequest {
    char endpoint[200];
    char token[1000];
    enum parser_status status;
} RegisterDeviceRequest;


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


int http_on_url(http_parser *parser, const char *at, size_t length) {
    struct http_parser_url url;
    RegisterDeviceRequest *parsed_request = parser->data;

    // Parse the request data
    //
    int r = http_parser_parse_url(at, length, 0, &url);
    if (r != 0) {
        ESP_LOGW(TAG, "Error parsing url (%d)", r);
        parsed_request->status = PARSER_STATUS_PARSE_URL_ERROR;
        return 0;
    }

    // Check if it asking for /register endpoint
    //
    const char endpoint[] = "/register";
    if (
            strlen(endpoint) != url.field_data[UF_PATH].len ||
            strncmp(endpoint, at + url.field_data[UF_PATH].off, url.field_data[UF_PATH].len) != 0
       ) {
        parsed_request->status = PARSER_STATUS_NOT_FOUND;
        return 0;
    }

    // Estract params from the query string
    //
    char *query = NULL;
    asprintf(&query, "%.*s", url.field_data[UF_QUERY].len, at + url.field_data[UF_QUERY].off);

    // Quick and dumb params extraction
    char *format;
    asprintf(&format, "endpoint=%%%d[^&]&token=%%%ds",
            sizeof(parsed_request->endpoint),
            sizeof(parsed_request->token));
    int parsed = sscanf(query, format, parsed_request->endpoint, parsed_request->token);

    free(format);
    free(query);

    if (parsed != 2) {
        parsed_request->status = PARSER_STATUS_INVALID_PARAMS;
        return 0;
    }

    parsed_request->status = PARSER_STATUS_OK;
    return 0;
}


esp_err_t get_device_id(char *device_id) {
    uint8_t mac_addr[6] = {0};
    esp_err_t ret = esp_efuse_mac_get_default(mac_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get base MAC address from BLK0 of EFUSE error (%s)", esp_err_to_name(ret));
        return ret;
    }
	sprintf(device_id, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    return ESP_OK;
}


int register_device_on_backend(const char *endpoint, const char *token, const char *device_id) {
    ESP_LOGI(TAG, ">>>> Send Request to Backend");

    RequestParams *request_params = calloc(1, sizeof(RequestParams));

    char *url = NULL;
    asprintf(&url, "http://%s", endpoint);
    strcpy(request_params->url, url);
    free(url);

    strcpy(request_params->token, token);

    snprintf(request_params->body, sizeof(request_params->body),
            "{\"device\": \"%s\"}", device_id);

    int status_code = http_request(event_group, request_params);

    free(request_params);

    ESP_LOGI(TAG, "<<<< Finish Request to Backend");
    return status_code;
}


void handle_request(struct netconn *conn, const char *request) {
    // 1. parse request
    //
    RegisterDeviceRequest *parsed_request = calloc(1, sizeof(RegisterDeviceRequest));
    struct http_parser *parser = calloc(1, sizeof(struct http_parser));
    struct http_parser_settings *parser_settings = calloc(1, sizeof(struct http_parser_settings));

    parser->data = parsed_request;
    parser_settings->on_url = http_on_url;

    http_parser_init(parser, HTTP_REQUEST);
    http_parser_execute(parser, parser_settings, request, strlen(request));

    free(parser);
    free(parser_settings);

    // 2. send response depending on request status
    //
    int status_code;
    char device_id[(2 + 1) * 6];
    switch(parsed_request->status) {
        case PARSER_STATUS_OK:
            // 3. send http request to the backend to register device
            if (get_device_id(device_id) != ESP_OK) {
                response(conn, 500, "Cannot obtain device id");
            } else {
                status_code = register_device_on_backend(parsed_request->endpoint, parsed_request->token, device_id);
                if (status_code == 200) {
                    response(conn, 200, device_id);
                } else {
                    response(conn, 500, "Cannot register device on backend");
                }
            }
            break;
        case PARSER_STATUS_NOT_FOUND:
            response(conn, 404, "");
            break;
        case PARSER_STATUS_INVALID_PARAMS:
            response(conn, 400, "Parameters `endpoint` and `token` are required");
            break;
        case PARSER_STATUS_PARSE_URL_ERROR:
            response(conn, 400, "Error parsing request");
            break;
        default:
            response(conn, 500, "Unknown parse status code");
            break;
    }
    free(parsed_request);
} // handle_request


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
