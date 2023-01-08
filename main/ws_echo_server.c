/* WebSocket Echo Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <stdlib.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "cJSON.h"
#include "bsp.h"

#include <esp_http_server.h>

#define HTTPD_WS_MAX_OPEN_SOCKETS  7  // copied from HTTPD_DEFAULT_CONFIG

/* A simple example that demonstrates using websocket echo server
 */
static const char *TAG = "ws_echo_server";

static httpd_handle_t m_server = NULL;

extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");


/*
 * Structure holding server handle
 * and internal socket fd in order
 * to use out of request send
 */
struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

/*
 * This handler echos back the received ws data
 * and triggers an async send if certain message received
 */
static esp_err_t echo_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        httpd_resp_set_type(req, "text/html");
        const uint32_t root_len = root_end - root_start;
        httpd_resp_send(req, root_start, root_len);
        return ESP_OK;
    }
    return ESP_OK;
}

static const httpd_uri_t ws = {
        .uri        = "/",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL,
        .is_websocket = true
};


esp_err_t httpd_ws_send_frame_to_all_clients(httpd_ws_frame_t *ws_pkt) {
    size_t fds = HTTPD_WS_MAX_OPEN_SOCKETS;
    int client_fds[HTTPD_WS_MAX_OPEN_SOCKETS] = {0};

    BSP_SOFTCHECK(httpd_get_client_list(m_server, &fds, client_fds));

    for (int i = 0; i < fds; i++) {
        httpd_ws_client_info_t client_info = httpd_ws_get_fd_info(m_server, client_fds[i]);
        ESP_LOGI(TAG, "Client type=%d fd=%d", client_info, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            ESP_LOGI(TAG, "Sending to ws client fd=%d", client_fds[i]);
            BSP_SOFTCHECK(httpd_ws_send_frame_async(m_server, client_fds[i], ws_pkt));
        }
    }

    return ESP_OK;
}


static void timer_cb(void*) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "rand1", rand());
    cJSON_AddNumberToObject(root, "rand2", rand());
    char *my_json_string = cJSON_Print(root);
    cJSON_Delete(root);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*) my_json_string;
    ws_pkt.len = strlen(my_json_string);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    httpd_ws_send_frame_to_all_clients(&ws_pkt);

    cJSON_free(my_json_string);
}


static esp_err_t ws_open_fn(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "Handle hd=%p: opening websocket fd=%d", hd, sockfd);
    return ESP_OK;
}


static void ws_close_fn(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "Handle hd=%p: closing websocket fd=%d", hd, sockfd);
}


static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = HTTPD_WS_MAX_OPEN_SOCKETS;
    config.open_fn = ws_open_fn;
    config.close_fn = ws_close_fn;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&m_server, &config) == ESP_OK) {
        // Registering the ws handler
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(m_server, &ws);
        ESP_LOGI(TAG, "Websocket server started");
        esp_timer_handle_t timer;
        bsp_start_periodic_timer(&timer, "httpd-my", timer_cb, NULL, 1);
        return m_server;
    }
    ESP_LOGI(TAG, "Error starting server!");

    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (m_server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(m_server) == ESP_OK) {
            m_server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    if (m_server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        start_webserver();
    }
}


void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &m_server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &m_server));

    /* Start the server for the first time */
    start_webserver();
}
