#include "esp_camera.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "led_strip.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "cam";

// ============================================================================
// LED Status
// ============================================================================
static led_strip_handle_t led_strip;

static void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    if (led_strip) {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
    }
}

static void init_led(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = 48,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

// ============================================================================
// NVS Helpers
// ============================================================================
static esp_err_t save_wifi_creds(const char *ssid, const char *pass) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;
    nvs_set_str(my_handle, "ssid", ssid);
    nvs_set_str(my_handle, "pass", pass);
    nvs_commit(my_handle);
    nvs_close(my_handle);
    return ESP_OK;
}

static esp_err_t load_wifi_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return err;
    err = nvs_get_str(my_handle, "ssid", ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(my_handle, "pass", pass, &pass_len);
    }
    nvs_close(my_handle);
    return err;
}

// ============================================================================
// Shared Frame Buffer
// ============================================================================
#define FRAME_BUFFER_SIZE (80 * 1024)
static uint8_t *shared_frame = NULL;
static size_t shared_frame_len = 0;
static SemaphoreHandle_t frame_mutex = NULL;
static volatile uint32_t frame_counter = 0;

// ============================================================================
// Camera Config
// ============================================================================
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_D7 16
#define CAM_PIN_D6 17
#define CAM_PIN_D5 18
#define CAM_PIN_D4 12
#define CAM_PIN_D3 10
#define CAM_PIN_D2 8
#define CAM_PIN_D1 9
#define CAM_PIN_D0 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

static void init_camera(void) {
  camera_config_t config = {.ledc_channel = LEDC_CHANNEL_0,
                            .ledc_timer = LEDC_TIMER_0,
                            .pin_d0 = CAM_PIN_D0,
                            .pin_d1 = CAM_PIN_D1,
                            .pin_d2 = CAM_PIN_D2,
                            .pin_d3 = CAM_PIN_D3,
                            .pin_d4 = CAM_PIN_D4,
                            .pin_d5 = CAM_PIN_D5,
                            .pin_d6 = CAM_PIN_D6,
                            .pin_d7 = CAM_PIN_D7,
                            .pin_xclk = CAM_PIN_XCLK,
                            .pin_pclk = CAM_PIN_PCLK,
                            .pin_vsync = CAM_PIN_VSYNC,
                            .pin_href = CAM_PIN_HREF,
                            .pin_sccb_sda = CAM_PIN_SIOD,
                            .pin_sccb_scl = CAM_PIN_SIOC,
                            .pin_pwdn = CAM_PIN_PWDN,
                            .pin_reset = CAM_PIN_RESET,
                            .xclk_freq_hz = 20000000,
                            .frame_size = FRAMESIZE_VGA, // Increased to 640x480
                            .pixel_format = PIXFORMAT_JPEG,
                            .grab_mode = CAMERA_GRAB_LATEST,
                            .fb_location = CAMERA_FB_IN_PSRAM,
                            .jpeg_quality = 12, // Higher quality (lower number)
                            .fb_count = 2};

  ESP_ERROR_CHECK(esp_camera_init(&config));
  ESP_LOGI(TAG, "Camera initialized");
}

// ============================================================================
// Camera Capture Task
// ============================================================================
static void camera_task(void *arg) {
  camera_fb_t *fb = NULL;

  while (1) {
    fb = esp_camera_fb_get();
    if (fb && fb->len > 0 && fb->len < FRAME_BUFFER_SIZE) {
      if (xSemaphoreTake(frame_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(shared_frame, fb->buf, fb->len);
        shared_frame_len = fb->len;
        frame_counter++;
        xSemaphoreGive(frame_mutex);
      }
      esp_camera_fb_return(fb);
    }
    // Minimal delay to allow other tasks to run, but capture as fast as possible
    vTaskDelay(pdMS_TO_TICKS(10)); 
  }
}

// ============================================================================
// HTTP Stream Handler
// ============================================================================
#define BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" BOUNDARY "\r\n";
static const char *STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
  uint8_t *buf = malloc(FRAME_BUFFER_SIZE);
  if (!buf)
    return ESP_FAIL;

  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);

  // Set 2s timeout
  struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
  setsockopt(httpd_req_to_sockfd(req), SOL_SOCKET, SO_SNDTIMEO, &timeout,
             sizeof(timeout));

  ESP_LOGI(TAG, "Client connected");
  
  uint32_t last_frame_sent = 0;

  while (1) {
    // Wait for a new frame
    if (last_frame_sent == frame_counter) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
    }

    size_t len = 0;

    // Copy frame
    if (xSemaphoreTake(frame_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (shared_frame_len > 0) {
        memcpy(buf, shared_frame, shared_frame_len);
        len = shared_frame_len;
        last_frame_sent = frame_counter;
      }
      xSemaphoreGive(frame_mutex);
    }

    if (len == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // Send frame
    char part_buf[64];
    if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) !=
        ESP_OK)
      break;
    size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, len);
    if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK)
      break;
    if (httpd_resp_send_chunk(req, (char *)buf, len) != ESP_OK)
      break;
      
    // No delay here - send next frame as soon as it's available
  }

  free(buf);
  ESP_LOGI(TAG, "Client disconnected");
  return ESP_OK;
}

// ============================================================================
// Wi-Fi & Provisioning
// ============================================================================
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        ESP_LOGI(TAG, "Connect failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2]))) {
                if (a >= 'a') a -= 'a'-'A';
                if (a >= 'A') a -= ('A' - 10);
                else a -= '0';
                if (b >= 'a') b -= 'a'-'A';
                if (b >= 'A') b -= ('A' - 10);
                else b -= '0';
                *dst++ = 16*a+b;
                src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = { .show_hidden = true };
    esp_wifi_scan_start(&scan_config, true);
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    char *resp_str = malloc(8192);
    if (!resp_str) {
        free(ap_list);
        return ESP_FAIL;
    }

    const char* style_css = 
        "<style>"
        "body{font-family:system-ui,-apple-system,sans-serif;background:#f0f2f5;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;padding:20px;box-sizing:border-box}"
        ".card{background:#fff;padding:2rem;border-radius:12px;box-shadow:0 4px 6px rgba(0,0,0,0.1);width:100%;max-width:400px}"
        "h2{margin-top:0;color:#1a1a1a;text-align:center;margin-bottom:1.5rem}"
        "label{display:block;margin-bottom:0.5rem;color:#4a5568;font-weight:500}"
        "input{width:100%;padding:0.75rem;margin-bottom:1rem;border:1px solid #e2e8f0;border-radius:6px;box-sizing:border-box;font-size:16px}"
        "button{width:100%;padding:0.75rem;background:#2563eb;color:#fff;border:none;border-radius:6px;font-weight:600;font-size:16px;cursor:pointer;transition:background .2s}"
        "button:hover{background:#1d4ed8}"
        ".net-list{list-style:none;padding:0;margin:1rem 0;max-height:200px;overflow-y:auto;border:1px solid #e2e8f0;border-radius:6px}"
        ".net-item{padding:0.75rem;border-bottom:1px solid #e2e8f0;cursor:pointer;transition:background .1s}"
        ".net-item:last-child{border-bottom:none}"
        ".net-item:hover{background:#f8fafc}"
        "</style>"
        "<script>"
        "function sel(ssid){document.getElementById('s').value=ssid;}"
        "function c(e){"
        "e.preventDefault();"
        "var d={ssid:document.getElementById('s').value,pass:document.getElementById('p').value};"
        "fetch('/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})"
        ".then(r=>r.text()).then(t=>{document.body.innerHTML='<div class=\"card\"><h2>'+t+'</h2></div>';});"
        "}"
        "</script>";

    strcpy(resp_str, "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>");
    strcat(resp_str, style_css);
    strcat(resp_str, "</head><body><div class='card'><h2>Setup Wi-Fi</h2><form onsubmit='c(event)'>");
    strcat(resp_str, "<label>Network</label><input id='s' name='ssid' placeholder='SSID' required>");
    strcat(resp_str, "<label>Password</label><input id='p' name='pass' type='password' placeholder='Password'>");
    strcat(resp_str, "<button type='submit'>Connect</button></form>");
    
    strcat(resp_str, "<h3>Available Networks</h3><ul class='net-list'>");
    for (int i = 0; i < ap_count; i++) {
        if (strlen((char *)ap_list[i].ssid) > 0) {
            char line[256];
            snprintf(line, sizeof(line), "<li class='net-item' onclick=\"sel('%s')\">%s <span style='float:right;color:#718096'>%d dBm</span></li>", 
                ap_list[i].ssid, ap_list[i].ssid, ap_list[i].rssi);
            strcat(resp_str, line);
        }
    }
    strcat(resp_str, "</ul></div></body></html>");
    
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(ap_list);
    free(resp_str);
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req) {
    char buf[1024];
    int ret, remaining = req->content_len;
    if (remaining >= sizeof(buf)) remaining = sizeof(buf) - 1;
    
    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = 0;

    char ssid[32] = {0};
    char pass[64] = {0};

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
        cJSON *pass_item = cJSON_GetObjectItem(root, "pass");
        if (ssid_item && cJSON_IsString(ssid_item)) {
            strncpy(ssid, ssid_item->valuestring, sizeof(ssid) - 1);
        }
        if (pass_item && cJSON_IsString(pass_item)) {
            strncpy(pass, pass_item->valuestring, sizeof(pass) - 1);
        }
        cJSON_Delete(root);
    }

    save_wifi_creds(ssid, pass);
    httpd_resp_send(req, "Saved. Restarting...", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static void start_provisioning_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &uri_root);
        httpd_uri_t uri_connect = { .uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler };
        httpd_register_uri_handler(server, &uri_connect);
    }
}

static void start_stream_server(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_open_sockets = 7;
  config.stack_size = 8192;
  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t uri = {
        .uri = "/", .method = HTTP_GET, .handler = stream_handler};
    httpd_register_uri_handler(server, &uri);
    ESP_LOGI(TAG, "Stream Server started");
  }
}

// ============================================================================
// Main
// ============================================================================
void app_main(void) {
  // Init NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Init LED
  init_led();
  set_led_color(0, 0, 0); // Off

  // Init Wi-Fi Common
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Try to load creds
  char ssid[32] = {0};
  char pass[64] = {0};
  if (load_wifi_creds(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK && strlen(ssid) > 0) {
      ESP_LOGI(TAG, "Found creds for %s. Connecting...", ssid);
      
      s_wifi_event_group = xEventGroupCreate();
      esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
      esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

      wifi_config_t wifi_config = {0};
      strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
      strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
      
      ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
      ESP_ERROR_CHECK(esp_wifi_start());

      EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
      
      if (bits & WIFI_CONNECTED_BIT) {
          ESP_LOGI(TAG, "Connected!");
          set_led_color(0, 0, 20); // Blue
          
          // Init Camera & Stream
          init_camera();
          shared_frame = heap_caps_malloc(FRAME_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
          frame_mutex = xSemaphoreCreateMutex();
          xTaskCreatePinnedToCore(camera_task, "cam", 4096, NULL, 5, NULL, 0);
          start_stream_server();
          return;
      }
      ESP_LOGI(TAG, "Failed to connect.");
  } else {
      ESP_LOGI(TAG, "No creds found.");
  }

  // Fallback to SoftAP
  ESP_LOGI(TAG, "Starting SoftAP...");
  set_led_color(20, 0, 0); // Red
  
  wifi_config_t ap_config = {
      .ap = {
          .ssid = "ESP32-CAM-SETUP",
          .ssid_len = strlen("ESP32-CAM-SETUP"),
          .channel = 1,
          .password = "",
          .max_connection = 4,
          .authmode = WIFI_AUTH_OPEN
      },
  };
  
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  
  start_provisioning_server();
}
