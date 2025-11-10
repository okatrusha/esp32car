#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"

// ===== WIFI AP MODE =====
const char* ssid = "ESP32-CAM-Car";
const char* password = "12345678";

// ===== CAMERA PINS =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ===== MOTOR PINS =====
#define MOTOR_R_PIN_1     14
#define MOTOR_R_PIN_2     15
#define MOTOR_L_PIN_1     13
#define MOTOR_L_PIN_2     12
#define LED_GPIO_NUM       4

int MOTOR_R_Speed = 170;
int MOTOR_L_Speed = 170;

// ===== STREAM CONSTANTS =====
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

// ===== HTML PAGE =====
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<html>
  <head>
    <title>ESP32 CAM Robot</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
  </head>
<body style="text-align:center;font-family:Arial">

<h2>ESP32-CAM Robot</h2>
<img id="stream" src="/stream" style="width:90%;border:2px solid black">

<br><br>
<button onclick="go('forward')">▲</button><br>
<button onclick="go('left')">◀</button>
<button onclick="go('stop')">●</button>
<button onclick="go('right')">▶</button><br>
<button onclick="go('backward')">▼</button><br><br>

<script>
function go(cmd) {
  fetch(`/action?go=`+cmd);
}
</script>

</body>
</html>
)rawliteral";

// ===== ROUTE HANDLERS =====
static esp_err_t index_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

// ---- SINGLE FRAME /capture ----
static esp_err_t capture_handler(httpd_req_t *req)
{
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return httpd_resp_send_500(req);

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return ESP_OK;
}

// ---- STREAM /stream ----
static esp_err_t stream_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  uint8_t *jpg_buf = NULL;
  size_t jpg_len = 0;

  httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);

  while (true)
  {
    fb = esp_camera_fb_get();
    if (!fb) break;

    if (fb->format != PIXFORMAT_JPEG) {
      bool ok = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
      esp_camera_fb_return(fb);
      fb = NULL;
      if (!ok) break;
    } else {
      jpg_buf = fb->buf;
      jpg_len = fb->len;
    }

    char buf[64];
    size_t hlen = snprintf(buf, 64, _STREAM_PART, jpg_len);
    if (httpd_resp_send_chunk(req, buf, hlen) != ESP_OK) break;
    if (httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len) != ESP_OK) break;
    if (httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY)) != ESP_OK) break;

    if (fb) esp_camera_fb_return(fb);
    else free(jpg_buf);

    jpg_buf = NULL;
  }
  return res;
}

// ---- ROBOT CONTROL /action ----
static esp_err_t action_handler(httpd_req_t *req)
{
  char query[50];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
  {
    if (strstr(query, "forward")) {
      analogWrite(MOTOR_R_PIN_1, 0); analogWrite(MOTOR_R_PIN_2, MOTOR_R_Speed);
      analogWrite(MOTOR_L_PIN_1, MOTOR_L_Speed); analogWrite(MOTOR_L_PIN_2, 0);
    }
    if (strstr(query, "backward")) {
      analogWrite(MOTOR_R_PIN_1, MOTOR_R_Speed); analogWrite(MOTOR_R_PIN_2, 0);
      analogWrite(MOTOR_L_PIN_1, 0); analogWrite(MOTOR_L_PIN_2, MOTOR_L_Speed);
    }
    if (strstr(query, "left")) {
      analogWrite(MOTOR_R_PIN_1, 0); analogWrite(MOTOR_R_PIN_2, MOTOR_R_Speed);
      analogWrite(MOTOR_L_PIN_1, 0); analogWrite(MOTOR_L_PIN_2, MOTOR_L_Speed);
    }
    if (strstr(query, "right")) {
      analogWrite(MOTOR_R_PIN_1, MOTOR_R_Speed); analogWrite(MOTOR_R_PIN_2, 0);
      analogWrite(MOTOR_L_PIN_1, MOTOR_L_Speed); analogWrite(MOTOR_L_PIN_2, 0);
    }
    if (strstr(query, "stop")) {
      analogWrite(MOTOR_R_PIN_1, 0); analogWrite(MOTOR_R_PIN_2, 0);
      analogWrite(MOTOR_L_PIN_1, 0); analogWrite(MOTOR_L_PIN_2, 0);
    }
  }
  return httpd_resp_send(req, "", HTTPD_RESP_USE_STRLEN);
}

// ===== START CAMERA SERVER =====
void startCameraServer()
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t index_uri   = {"/",      HTTP_GET, index_handler,   NULL};
  httpd_uri_t cmd_uri     = {"/action",HTTP_GET, action_handler,  NULL};
  httpd_uri_t capture_uri = {"/capture",HTTP_GET, capture_handler, NULL};
  httpd_uri_t stream_uri  = {"/stream",HTTP_GET, stream_handler,  NULL};

  // PORT 80 (web + control + capture)
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
  }

  // PORT 81 (video stream)
  config.server_port = 81;
  config.ctrl_port = 32768;
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

// ===== SETUP =====
void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(MOTOR_R_PIN_1, OUTPUT);
  pinMode(MOTOR_R_PIN_2, OUTPUT);
  pinMode(MOTOR_L_PIN_1, OUTPUT);
  pinMode(MOTOR_L_PIN_2, OUTPUT);
  pinMode(LED_GPIO_NUM, OUTPUT);

  Serial.begin(115200);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) return;

  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  startCameraServer();
}

// ===== LOOP =====
void loop() {}
