#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define SS_PIN 16
#define RST_PIN 17
#define BUZZER_PIN 5
#define LED_PIN 4
#define CAM_PIN 13
#define RELAY_PIN 27 // Chân điều khiển relay cho solenoid

// OLED SH1106
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi credentials
const char* ssid = "...";
const char* password = "...";

// Telegram Bot credentials
#define BOTtoken "8014020245:AAHmHYLlKFedWZZD7wtFlA64kmgDyMjlDZI"
#define CHAT_ID "1851298938"

MFRC522 rfid(SS_PIN, RST_PIN);
String uidString;

String validUIDs[10]; // Mảng chứa tối đa 10 UID hợp lệ (có thể tăng nếu cần)
int numValidUIDs = 0;

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

unsigned long lastCheckTime = 0;
const long checkInterval = 1000;
int WrongScan = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(CAM_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT); // Khởi tạo chân relay
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(CAM_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW); // Relay tắt (active LOW)

  // Kết nối WiFi
  connectWiFi();

  SPI.begin();
  rfid.PCD_Init();

  // Khởi động OLED
  Wire.begin(21, 22);
  if (!display.begin(SCREEN_ADDRESS, true)) {
    Serial.println("SH1106 không tìm thấy!");
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(10, 10);
  display.println("Vui long quet the!");
  display.display();

  // Thêm UID mặc định ban đầu
  addUID("FA73F804");
  sendTelegramMessage("Hệ thống đã khởi động! Gửi /open để mở cửa, /adduid <UID> để thêm, /deleteuid <UID> để xóa, /uidlist để xem danh sách.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Reconnecting...");
    connectWiFi();
  }

  if (millis() - lastCheckTime > checkInterval) {
    handleTelegramMessages();
    lastCheckTime = millis();
  }

  readUID();
  delay(500);
}

void connectWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("WiFi connected!");
}

void updateOLED(const char* line1, const char* line2 = "") {
  display.clearDisplay();
  display.setCursor(10, 20);
  display.println(line1);
  if (line2[0] != '\0') {
    display.setCursor(10, 40);
    display.println(line2);
  }
  display.display();
}

void activateSolenoid() {
  digitalWrite(RELAY_PIN, HIGH); // Bật relay (active HIGH)
  delay(1000); // Giữ solenoid mở trong 1 giây
  digitalWrite(RELAY_PIN, LOW); // Tắt relay
}

void beep(bool isValid) {
  if (isValid) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
      delay(100);
      digitalWrite(LED_PIN, HIGH);
      delay(500);
      digitalWrite(LED_PIN, LOW);
    }
    digitalWrite(CAM_PIN, LOW);
    activateSolenoid(); // Kích hoạt solenoid
    WrongScan = 0;
  } else {
    if (WrongScan < 3) {
      digitalWrite(CAM_PIN, HIGH);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(1500);
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(CAM_PIN, LOW);
      WrongScan++;
    } else {
      for (int i = 0; i < 5; i++) {
        digitalWrite(CAM_PIN, HIGH);
        delay(100);
        digitalWrite(CAM_PIN, LOW);
      }
      digitalWrite(BUZZER_PIN, HIGH);
      delay(5000);
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

bool isValidUID(String uid) {
  for (int i = 0; i < numValidUIDs; i++) {
    if (uid == validUIDs[i]) {
      return true;
    }
  }
  return false;
}

void addUID(String uid) {
  if (numValidUIDs < 10 && !isValidUID(uid)) { // Giới hạn 10 UID, tránh trùng
    validUIDs[numValidUIDs] = uid;
    numValidUIDs++;
    Serial.println("Added UID: " + uid);
    sendTelegramMessage("Đã thêm UID " + uid + " vào danh sách hợp lệ.");
  } else if (isValidUID(uid)) {
    sendTelegramMessage("UID " + uid + " đã tồn tại trong danh sách!");
  } else {
    sendTelegramMessage("Danh sách UID đã đầy (tối đa 10)!");
  }
}

void deleteUID(String uid) {
  for (int i = 0; i < numValidUIDs; i++) {
    if (validUIDs[i] == uid) {
      for (int j = i; j < numValidUIDs - 1; j++) {
        validUIDs[j] = validUIDs[j + 1];
      }
      numValidUIDs--;
      Serial.println("Deleted UID: " + uid);
      sendTelegramMessage("Đã xóa UID " + uid + " khỏi danh sách hợp lệ.");
      return;
    }
  }
  sendTelegramMessage("UID " + uid + " không tồn tại trong danh sách!");
}

void showUIDList() {
  String message = "Danh sách UID hợp lệ:\n";
  if (numValidUIDs == 0) {
    message += "Không có UID nào.";
  } else {
    for (int i = 0; i < numValidUIDs; i++) {
      message += validUIDs[i] + "\n";
    }
  }
  sendTelegramMessage(message);
}

void sendTelegramMessage(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    bot.sendMessage(CHAT_ID, message, "");
  }
}

void handleTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = String(bot.messages[i].chat_id);
      String text = bot.messages[i].text;
      if (chat_id != CHAT_ID) {
        bot.sendMessage(chat_id, "Bạn không có quyền sử dụng bot này!", "");
        continue;
      }
      if (text.startsWith("/adduid ")) {
        String newUID = text.substring(8); // Lấy phần sau /adduid
        newUID.toUpperCase();
        if (newUID.length() == 8) { // Giả định UID là 8 ký tự (4 byte HEX)
          addUID(newUID);
        } else {
          bot.sendMessage(chat_id, "UID không hợp lệ! Phải là 8 ký tự (ví dụ: FA73F804).", "");
        }
      } else if (text.startsWith("/deleteuid ")) {
        String delUID = text.substring(11); // Lấy phần sau /deleteuid
        delUID.toUpperCase();
        if (delUID.length() == 8) {
          deleteUID(delUID);
        } else {
          bot.sendMessage(chat_id, "UID không hợp lệ! Phải là 8 ký tự (ví dụ: FA73F804).", "");
        }
      } else if (text == "/uidlist") {
        showUIDList();
      } else if (text == "/open") {
        sendTelegramMessage("UID OPEN đã vào nhà");
        beep(true); // Kích hoạt solenoid cùng với còi và LED
      } else {
        bot.sendMessage(chat_id, "Lệnh không hợp lệ! Sử dụng: /open, /adduid <UID>, /deleteuid <UID>, /uidlist.", "");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

void readUID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  
  uidString = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uidString += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    uidString += String(rfid.uid.uidByte[i], HEX);
  }
  uidString.toUpperCase();
  Serial.println("Card UID: " + uidString);

  if (isValidUID(uidString)) {
    beep(true);
    updateOLED("Quet the thanh cong!", uidString.c_str());
    sendTelegramMessage("UID " + uidString + " đã vào nhà");
  } else {
    beep(false);
    updateOLED("Quet the that bai!");
    sendTelegramMessage("Có UID lạ " + uidString + " đang cố gắng vào nhà!!!");
  }

  delay(2000);
  rfid.PICC_HaltA();
  updateOLED("Vui long quet the!"); // Quay lại giao diện ban đầu
}