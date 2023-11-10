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
        ESP_LOGE(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

		uint8_t buf[128] = { 0 };
		ws_pkt.payload = buf;
		ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 128);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGE(TAG, "frame len is %d", ws_pkt.len);
    ESP_LOGE(TAG, "Packet type: %d", ws_pkt.type);

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
    return ret;	

}


  class HTTPMessage
  {
    public:
      HTTPMessage(httpd_req_t *req) : req_(req) {}

			void SetHeader(const char *key, const char *value) {
				httpd_resp_set_hdr(req_,key,value);
			}

      void SendResponse(uint16_t code, const char *val, const char *content_type=nullptr)
      {
        if(content_type)
          httpd_resp_set_type(req_,content_type);
        else 
          httpd_resp_set_type(req_,"text/plain");

        if(code != 200)
          httpd_resp_set_status(req_,HTTPD_404);
        else
          httpd_resp_set_status(req_,HTTPD_200);

        httpd_resp_send(req_,val,HTTPD_RESP_USE_STRLEN);
      }
    private:
      httpd_req_t* req_;
  };




static esp_err_t root_handler(httpd_req_t *req)
{
	HTTPMessage msg(req);
	msg.SendResponse(200,R"HTML0(<!DOCTYPE html>
<html>
<head>
<title>Page Title</title>
</head>
<body style='background-color: #EEEEEE;'>

<span style='color: #003366;'>

<h1>Lets generate a random number</h1>
<p>The first random number is: <span id='rand1'>-</span></p>
<p>The second random number is: <span id='rand2'>-</span></p>
<p><button type='button' id='BTN_SEND_BACK'>
Send info to ESP32
</button></p>

</span>

</body>
<script>
  var Socket;
  document.getElementById('BTN_SEND_BACK').addEventListener('click', button_send_back);
  function init() {
    Socket = new WebSocket('ws://' + window.location.hostname + ':81/ws');
    console.log("opened " + Socket);
    Socket.onmessage = function(event) {
      processCommand(event);
    };
  }
  function button_send_back() {
    var msg = {
    brand: 'Gibson',
    type: 'Les Paul Studio',
    year:  2010,
    color: 'white'
    };
    Socket.send(JSON.stringify(msg));
  }
  function processCommand(event) {
    var obj = JSON.parse(event.data);
    document.getElementById('rand1').innerHTML = obj.rand1;
    document.getElementById('rand2').innerHTML = obj.rand2;
    console.log(obj.rand1);
    console.log(obj.rand2);
  }
  window.onload = function(event) {
    init();
  }
</script>
</html>)HTML0","text/html");
	return ESP_OK;
}


static const httpd_uri_t root = {
        .uri        = "/",
        .method     = HTTP_GET,
        .handler    = root_handler,
        .user_ctx   = NULL,
};


static const httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = echo_handler,
        .user_ctx   = NULL,
        .is_websocket = true
};



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
		config.server_port = 81;
		config.ctrl_port = 32767;


    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&m_server, &config) == ESP_OK) {
        // Registering the ws handler
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(m_server, &ws);
        httpd_register_uri_handler(m_server, &root);
        ESP_LOGI(TAG, "Websocket server started");
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


extern "C" void app_main(void)
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
