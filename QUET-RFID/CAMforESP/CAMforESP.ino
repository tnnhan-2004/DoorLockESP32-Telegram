#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"

// WiFi config
const char* ssid = "...";
const char* password = "...";

// Telegram
String botToken = "...";
String chatID = "...";

// GPIO
#define TRIGGER_PIN 4     // GPIO 4 để chụp ảnh
#define PIR_PIN 13        // GPIO 13 để phát hiện chuyển động
#define LED_PIN 33        // GPIO để điều khiển đèn (nên là GPIO 33, 12, 14...)

unsigned long lastMotionTime = 0;
unsigned long motionCooldown = 10000; // 10 giây cooldown

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

void setup() {
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // WiFi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi đã kết nối");

  // Camera config
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
  config.xclk_freq_hz = 8000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Lỗi camera!");
    return;
  }
  Serial.println("Camera đã sẵn sàng.");
}

void loop() {
  // Chụp ảnh khi nhận tín hiệu từ GPIO4
  if (digitalRead(TRIGGER_PIN) == HIGH) {
    Serial.println("Chụp ảnh theo tín hiệu GPIO4.");
    sendPhotoToTelegram();
    while (digitalRead(TRIGGER_PIN) == HIGH) delay(100);
  }

  // Kiểm tra PIR (có người đến gần)
  if (digitalRead(PIR_PIN) == HIGH && millis() - lastMotionTime > motionCooldown) {
    lastMotionTime = millis();
    Serial.println("Phát hiện chuyển động!");
    digitalWrite(LED_PIN, HIGH);
    sendMotionAlertToTelegram();
    delay(2000); // Giữ đèn sáng 2 giây
    digitalWrite(LED_PIN, LOW);
  }

  delay(100);
}

void sendMotionAlertToTelegram() {
  WiFiClientSecure client;
  client.setInsecure();

  if (client.connect("api.telegram.org", 443)) {
    String url = "/bot" + botToken + "/sendMessage?chat_id=" + chatID + "&text=🚨 Phát hiện chuyển động gần camera!";
    client.println("GET " + url + " HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Connection: close");
    client.println();
    Serial.println("Đã gửi cảnh báo chuyển động.");
  } else {
    Serial.println("Lỗi kết nối khi gửi cảnh báo.");
  }
}

void sendPhotoToTelegram() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Lỗi chụp ảnh!");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  if (client.connect("api.telegram.org", 443)) {
    String boundary = "----WebKitFormBoundary";
    String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + chatID + "\r\n";
    head += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--" + boundary + "--\r\n";

    client.println("POST /bot" + botToken + "/sendPhoto HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Content-Length: " + String(head.length() + fb->len + tail.length()));
    client.println();
    client.print(head);
    client.write(fb->buf, fb->len);
    client.print(tail);
    Serial.println("Đã gửi ảnh.");
  } else {
    Serial.println("Lỗi gửi ảnh.");
  }

  esp_camera_fb_return(fb);
}