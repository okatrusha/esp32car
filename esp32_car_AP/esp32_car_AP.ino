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
const char* ssid = "ESP32-CAM-Car-01";
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

httpd_handle_t camera_httpd = NULL;

// ===== CONTROL PANEL HTML (TEXT BUTTONS + SPEED CONTROL) =====
static const char PROGMEM CONTROL_HTML[] = R"rawliteral(
<html>
<head>
<title>ESP32-CAM Robot</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { text-align:center; font-family:Arial; }
  img  { width:60%; border:2px solid #444; border-radius:10px; margin-top:10px; }
  button { font-size:22px; padding:16px 28px; margin:10px; }
</style>
</head>

<body>

<h2>ESP32-CAM Robot Control</h2>

<!-- CAMERA SNAPSHOT -->
<img id="frame" src="/capture">

<script>
// refresh camera image every 250 ms
setInterval(() => {
  document.getElementById("frame").src = "/capture?" + Date.now();
}, 250);

// send movement commands
function go(cmd) {
  fetch("/action?go=" + cmd);
}

// update speed display
setInterval(() => {
  fetch("/speed").then(r => r.text()).then(t => {
    document.getElementById("spd").textContent = "Speed: " + t;
  });
}, 500);
</script>

<br>

<button onclick="go('forward')">Forward</button><br>

<button onclick="go('left')">Left</button>
<button onclick="go('stop')">Stop</button>
<button onclick="go('right')">Right</button><br>

<button onclick="go('backward')">Backward</button><br><br>

<button onclick="go('minus')">Speed -</button>
<button onclick="go('plus')">Speed +</button>

<p id="spd">Speed: ...</p>

</body>
</html>
)rawliteral";

// ===== HANDLERS =====

// ---- CONTROL PANEL ----
static esp_err_t control_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, CONTROL_HTML, strlen(CONTROL_HTML));
}

// ---- CAPTURE IMAGE ----
static esp_err_t capture_handler(httpd_req_t *req)
{
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return httpd_resp_send_500(req);

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_send(req, (const char *)fb->buf, fb->len);

  esp_camera_fb_return(fb);
  return ESP_OK;
}

// ---- SPEED VALUE ENDPOINT ----
static esp_err_t speed_handler(httpd_req_t *req)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", (MOTOR_L_Speed + MOTOR_R_Speed) / 2);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, buf, strlen(buf));
}

// ---- MOVEMENT + SPEED CONTROL ----
static esp_err_t action_handler(httpd_req_t *req)
{
  char query[50];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
  {
    if (strstr(query, "forward")) {
      analogWrite(MOTOR_R_PIN_1, 0);
      analogWrite(MOTOR_R_PIN_2, MOTOR_R_Speed);
      analogWrite(MOTOR_L_PIN_1, MOTOR_L_Speed);
      analogWrite(MOTOR_L_PIN_2, 0);
    }

    if (strstr(query, "backward")) {
      analogWrite(MOTOR_R_PIN_1, MOTOR_R_Speed);
      analogWrite(MOTOR_R_PIN_2, 0);
      analogWrite(MOTOR_L_PIN_1, 0);
      analogWrite(MOTOR_L_PIN_2, MOTOR_L_Speed);
    }

    if (strstr(query, "left")) {
      analogWrite(MOTOR_R_PIN_1, 0);
      analogWrite(MOTOR_R_PIN_2, MOTOR_R_Speed);
      analogWrite(MOTOR_L_PIN_1, 0);
      analogWrite(MOTOR_L_PIN_2, MOTOR_L_Speed);
    }

    if (strstr(query, "right")) {
      analogWrite(MOTOR_R_PIN_1, MOTOR_R_Speed);
      analogWrite(MOTOR_R_PIN_2, 0);
      analogWrite(MOTOR_L_PIN_1, MOTOR_L_Speed);
      analogWrite(MOTOR_L_PIN_2, 0);
    }

    if (strstr(query, "stop")) {
      analogWrite(MOTOR_R_PIN_1, 0);
      analogWrite(MOTOR_R_PIN_2, 0);
      analogWrite(MOTOR_L_PIN_1, 0);
      analogWrite(MOTOR_L_PIN_2, 0);
    }

    // ---- SPEED + ----
    if (strstr(query, "plus")) {
      MOTOR_L_Speed += 30;
      MOTOR_R_Speed += 30;
      if (MOTOR_L_Speed > 255) MOTOR_L_Speed = 255;
      if (MOTOR_R_Speed > 255) MOTOR_R_Speed = 255;
    }

    // ---- SPEED - ----
    if (strstr(query, "minus")) {
      MOTOR_L_Speed -= 30;
      MOTOR_R_Speed -= 30;
      if (MOTOR_L_Speed < 85) MOTOR_L_Speed = 85;
      if (MOTOR_R_Speed < 85) MOTOR_R_Speed = 85;
    }
  }

  return httpd_resp_send(req, "", HTTPD_RESP_USE_STRLEN);
}

// ===== START SERVER =====
void startCameraServer()
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t control_uri  = {"/",        HTTP_GET, control_handler, NULL};
  httpd_uri_t control2_uri = {"/control", HTTP_GET, control_handler, NULL};
  httpd_uri_t capture_uri  = {"/capture", HTTP_GET, capture_handler, NULL};
  httpd_uri_t action_uri   = {"/action",  HTTP_GET, action_handler,  NULL};
  httpd_uri_t speed_uri    = {"/speed",   HTTP_GET, speed_handler,   NULL};

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &control_uri);
    httpd_register_uri_handler(camera_httpd, &control2_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &action_uri);
    httpd_register_uri_handler(camera_httpd, &speed_uri);
  }

  Serial.println("HTTP server ready. Open: http://192.168.4.1/");
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
  delay(200);

  // ---- CAMERA CONFIG ----
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

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed!");
    return;
  }

  // ---- WIFI AP ----
  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  startCameraServer();
}

// ===== LOOP =====
void loop() {}
