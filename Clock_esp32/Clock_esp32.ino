#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==== OLED ====
#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ==== WIFI ====
const char *ssid = "TP-Link71_IoT";
const char *password = "28465555";

// ==== Timezone ====
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const char *time_zone = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// ==== Buttons ====
#define BTN_MENU 32
#define BTN_UP   14
#define BTN_DOWN 27

// ==== Buzzer ====
#define BUZZER_PIN 25

// ==== MENU MODES ====
enum Mode {
  MODE_CLOCK = 0,
  MODE_TIMER,
  MODE_WIFI,
  MODE_ANIMATION,
  MODE_ANIMATION1
};


// =============ROCKET

static const unsigned char PROGMEM rocket_16x16[] = {
  0x03,0x80,
  0x07,0xC0,
  0x0F,0xE0,
  0x1C,0x70,
  0x38,0x38,
  0x70,0x1C,
  0x70,0x1C,
  0x7F,0xFC,
  0x7F,0xFC,
  0x70,0x1C,
  0x70,0x1C,
  0x38,0x38,
  0x1C,0x70,
  0x0F,0xE0,
  0x07,0xC0,
  0x03,0x80
};


Mode currentMode = MODE_CLOCK;

// Timer variables
int timerSeconds = 15 * 60; // 15 minutes
bool timerRunning = false;

// ==== HELPER OLED ====
void clearOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

// ==== Beep helper ====
void beep(int freq = 2000, int duration = 120) {
  tone(BUZZER_PIN, freq, duration);
  delay(duration + 20);
  noTone(BUZZER_PIN);
}

// ==== NTP ====
void timeavailable(struct timeval *t) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Time synced!");
  display.display();
  delay(800);
}

// ==== DRAW CLOCK ====
void drawClock() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  clearOLED();
  char timeStr[16];
  char dateStr[16];

  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);

  display.setTextSize(2);
  display.setCursor(10, 0);
  display.println(timeStr);

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println(dateStr);
  display.display();
}

// ==== DRAW TIMER ====
void drawTimer() {
  clearOLED();

  int m = timerSeconds / 60;
  int s = timerSeconds % 60;

  display.setTextSize(2);
  display.setCursor(10, 0);
  display.printf("%02d:%02d", m, s);

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println(timerRunning ? "Running..." : "Paused");
  display.display();
}

// ==== DRAW WIFI ====
void drawWiFi() {
  clearOLED();

  long rssi = WiFi.RSSI();

  display.setTextSize(2);
  display.setCursor(10, 0);
  display.print("WiFi:");
  display.print(rssi);

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println("dBm RSSI");
  display.display();
}

// ==== SIMPLE ANIMATION ====
int dotPos = 0;

void drawAnimation() {
  clearOLED();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("GO WORKING NOW ");

  for (int i = 0; i < dotPos; i++) {
    display.print(">");
  }

  dotPos = (dotPos + 1) % 6;

  display.display();
}

// ===== ROCKET ANIMATION =======

void animateRocket() {
  for (int x = 128; x > -16; x--) {
    display.clearDisplay();
    display.drawBitmap(x, 24, rocket_16x16, 16, 16, WHITE);
    display.display();
    delay(10);  // швидкість анімації (збільши/зменш)
  }
}













// ==== BUTTON HANDLING ====
bool btnPressed(int pin) {
  return digitalRead(pin) == LOW;
}

void handleButtons() {
    // Перемикання режимів
    if(btnPressed(BTN_MENU)){
        currentMode = (Mode)((currentMode + 1) % 5);
        beep();
        delay(200);
    }

    if(currentMode == MODE_TIMER){
        // UP → збільшити або старт
        if(btnPressed(BTN_UP)){
            if(!timerRunning) timerRunning = true; // старт таймера
            timerSeconds += 60; // +1 хв
            beep();
            delay(200);
        }

        // DOWN → зменшити або пауза
        if(btnPressed(BTN_DOWN)){
            if(timerRunning){
                timerRunning = false; // пауза
            } else if(timerSeconds > 10){
                timerSeconds -= 60; // -1 хв
            }
            beep();
            delay(200);
        }
    }
}


// ==== SETUP ====
void setup() {
  Serial.begin(115200);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 failed");
    while (1);
  }

  // Buttons
  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Connecting WiFi...");
  display.display();

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  // NTP
  esp_sntp_servermode_dhcp(1);
  sntp_set_time_sync_notification_cb(timeavailable);
  configTzTime(time_zone, ntpServer1, ntpServer2);

  delay(500);
}

// ==== LOOP ====
unsigned long lastTimerTick = 0;

void loop() {
  handleButtons();

  // Timer countdown
  if (currentMode == MODE_TIMER && timerRunning && millis() - lastTimerTick >= 1000) {
    lastTimerTick = millis();
    timerSeconds--;

    if (timerSeconds <= 0) {
      // Timer finished
      for (int i = 0; i < 5; i++) {
        beep(1000, 200);
        delay(150);
      }
      timerRunning = false;
      timerSeconds = 15 * 60;
    }
  }

  // Draw current mode
  switch (currentMode) {
    case MODE_CLOCK:     drawClock(); break;
    case MODE_TIMER:     drawTimer(); break;
    case MODE_WIFI:      drawWiFi(); break;
    case MODE_ANIMATION: drawAnimation(); break;
    case MODE_ANIMATION1: animateRocket(); break;
  }

  delay(100);
}
