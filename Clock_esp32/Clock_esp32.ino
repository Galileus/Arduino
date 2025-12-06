#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- OLED ----------
#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ---------- WiFi / Time ----------
const char *ssid = "TP-Link71_IoT";
const char *password = "28465555";
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
// Europe/Kyiv with DST rules
const char *time_zone = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// ---------- Pins (from your message) ----------
const int BTN_MODE_PIN = 32;   // cycle modes
const int BTN_UP_PIN   = 14;   // timer up / start/pause
const int BTN_DOWN_PIN = 27;   // timer down / reset
const int BUZZER_PIN   = 25;   // buzzer (use ledc)

// ---------- Button debounce ----------
const unsigned long DEBOUNCE_MS = 50;
struct Button {
  int pin;
  bool activeLow;
  bool state;           // current stable state (true = pressed)
  bool lastRaw;
  unsigned long lastChange;
  unsigned long lastPressTime; // for press detection
};
Button btnMode = {BTN_MODE_PIN, true, false, HIGH, 0, 0};
Button btnUp   = {BTN_UP_PIN, true, false, HIGH, 0, 0};
Button btnDown = {BTN_DOWN_PIN, true, false, HIGH, 0, 0};

void setupButton(Button &b) {
  pinMode(b.pin, INPUT_PULLUP); // using INPUT_PULLUP, active low
  b.lastRaw = digitalRead(b.pin);
  b.state = (b.activeLow ? (b.lastRaw == LOW) : (b.lastRaw == HIGH));
  b.lastChange = millis();
}

// read and debounce, update b.state, return true when a new pressed event detected
bool updateButton(Button &b) {
  bool raw = digitalRead(b.pin);
  unsigned long now = millis();
  if (raw != b.lastRaw) {
    b.lastChange = now;
    b.lastRaw = raw;
  }
  if (now - b.lastChange > DEBOUNCE_MS) {
    bool pressed = b.activeLow ? (raw == LOW) : (raw == HIGH);
    if (pressed != b.state) {
      b.state = pressed;
      if (pressed) {
        b.lastPressTime = now;
        return true; // new press event
      }
    }
  }
  return false;
}

// ---------- Modes ----------
enum Mode {
  MODE_CLOCK = 0,
  MODE_TIMER_SET,
  MODE_COUNTDOWN,
  MODE_WIFI,
  MODE_ANIM,
  MODE_COUNT
};
Mode currentMode = MODE_CLOCK;

// ---------- Timer (minutes 0..15) ----------
int timerSetMinutes = 60; // default
unsigned long timerRemainingMs = 0; // when running
bool timerRunning = false;
unsigned long timerLastTickMs = 0;

// ---------- Buzzer (use LEDC channel for tone) ----------
const int BUZZER_LEDC_CHANNEL = 0;
const int BUZZER_LEDC_FREQ = 2000; // starting freq (not used)
void buzzerBeep(int freq, unsigned long durationMs) {
  ledcWriteTone(BUZZER_LEDC_CHANNEL, freq);
  delay(durationMs);
  ledcWriteTone(BUZZER_LEDC_CHANNEL, 0); // stop
}

// ---------- Display helpers ----------
void oledClear() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

void oledCenteredTextY(const char *txt, int y, uint8_t size = 1) {
  display.setTextSize(size);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  int x = max(0, (OLED_WIDTH - (int)w) / 2);
  display.setCursor(x, y);
  display.println(txt);
}

// ---------- SNTP callback ----------
void timeavailable(struct timeval *t) {
  // brief on-screen feedback (non-blocking handled elsewhere)
  // we won't use delay here to avoid blocking.
}

// ---------- WiFi signal to bars helper ----------
int rssiToBars(int rssi) {
  if (rssi > -55) return 4;
  if (rssi > -65) return 3;
  if (rssi > -75) return 2;
  if (rssi > -85) return 1;
  return 0;
}

// ---------- Animation state ----------
int animPos = 0;
int animDir = 1;
unsigned long animLastMs = 0;

// ---------- Timing control ----------
unsigned long lastDisplayUpdate = 0;
const unsigned long CLOCK_UPDATE_MS = 1000;  // 1s for clock, timer, wifi
const unsigned long ANIM_UPDATE_MS = 100;    // faster for animation

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while (true) delay(10);
  }
  oledClear();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Hi there !!!");
  display.display();
  delay(800);

  // Buttons
  setupButton(btnMode);
  setupButton(btnUp);
  setupButton(btnDown);

  // Buzzer via LEDC
  tone(BUZZER_PIN, 2000);
  noTone(BUZZER_PIN);

  // WiFi init (must be before SNTP)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  oledClear();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Connecting to:");
  display.setCursor(0, 10);
  display.println(ssid);
  display.display();

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    // simple timeout behavior but still wait
    if (millis() - wifiStart > 10000) {
      // keep trying but update message
      oledClear();
      display.setCursor(0, 0);
      display.println("Connecting to:");
      display.println(ssid);
      display.println();
      display.println("Still trying...");
      display.display();
      wifiStart = millis();
    }
    delay(200);
    updateButton(btnMode); updateButton(btnUp); updateButton(btnDown); // allow pressing during connect
  }

  // show connected
  oledClear();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi connected!");
  display.setCursor(0, 10);
  display.print("IP: ");
  display.println(WiFi.localIP().toString());
  display.display();
  delay(800);

  // SNTP safe init (after WiFi started)
  esp_sntp_servermode_dhcp(1);
  sntp_set_time_sync_notification_cb(timeavailable);
  configTzTime(time_zone, ntpServer1, ntpServer2);

  // small pause for initial sync
  delay(200);

  // initialize timer values
  timerSetMinutes = 15;
  timerRunning = false;
  timerRemainingMs = (unsigned long)timerSetMinutes * 60UL * 1000UL;

  lastDisplayUpdate = 0;
  animLastMs = millis();
}

// ---------- Helpers to draw pages ----------
void drawClockPage() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    oledClear();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Waiting NTP...");
    display.display();
    return;
  }

  oledClear();
  // Big time
  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  display.setTextSize(2);
  display.setCursor(10, 0);
  display.println(timeStr);

  // Date bottom
  char dateStr[20];
  strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println(dateStr);

  display.display();
}

void drawTimerSetPage() {
  oledClear();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Set timer (min):");
  display.setTextSize(2);
  char tbuf[8];
  snprintf(tbuf, sizeof(tbuf), "%02d:00", timerSetMinutes);
  display.setCursor(24, 6);
  display.println(tbuf);

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println("UP/DOWN adjusts, MODE to run");
  display.display();
}

void drawCountdownPage() {
  oledClear();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(timerRunning ? "Countdown (running)" : "Countdown (paused)");
  // show remaining mm:ss
  unsigned long rem = timerRemainingMs / 1000; // seconds
  int mm = rem / 60;
  int ss = rem % 60;
  char buf[10];
  snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);
  display.setTextSize(2);
  display.setCursor(28, 6);
  display.println(buf);

  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println("UP=start/pause  DOWN=reset");
  display.display();
}

void drawWifiPage() {
  oledClear();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi signal:");
  long rssi = WiFi.RSSI();
  int bars = rssiToBars((int)rssi);

  // draw bar icons simple
  int baseX = 90;
  int baseY = 8;
  for (int i = 0; i < 4; ++i) {
    int x = baseX + i*8;
    int h = (i+1) * 4;
    int y = baseY + (12 - h);
    if (i < bars) {
      display.fillRect(x, y, 6, h, SSD1306_WHITE);
    } else {
      display.drawRect(x, y, 6, h, SSD1306_WHITE);
    }
  }

  char rbuf[30];
  snprintf(rbuf, sizeof(rbuf), "RSSI: %ld dBm", rssi);
  display.setTextSize(1);
  display.setCursor(0, 14);
  display.println(rbuf);
  display.display();
}

void drawAnimPage() {
  oledClear();

  // bouncing dot across width
  int x = animPos;
  int y = 12;
  display.fillCircle(x, y, 2, SSD1306_WHITE);

  // label
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Animation");

  display.display();
}

// flash TIME UP screen and beep
void timeUpAlarm() {
  // beep pattern and flash
  for (int i=0; i<6; ++i) {
    oledClear();
    if (i % 2 == 0) {
      display.setTextSize(2);
      display.setCursor(8, 6);
      display.println("TIME UP!");
    } else {
      // screen blank - already clear
    }
    display.display();
    ledcWriteTone(BUZZER_LEDC_CHANNEL, (i%2==0)?1000:0);
    delay(400);
  }
  ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
}

// ---------- Main loop ----------
void loop() {
  unsigned long now = millis();

  // update buttons (debounced)
  bool modePressed = updateButton(btnMode);
  bool upPressed = updateButton(btnUp);
  bool downPressed = updateButton(btnDown);

  // handle mode button press (cycle)
  if (modePressed) {
    currentMode = Mode((currentMode + 1) % MODE_COUNT);
    // small visual feedback
    oledClear();
    display.setTextSize(1);
    display.setCursor(0, 0);
    switch (currentMode) {
      case MODE_CLOCK: display.println("Mode: CLOCK"); break;
      case MODE_TIMER_SET: display.println("Mode: TIMER SET"); break;
      case MODE_COUNTDOWN: display.println("Mode: COUNTDOWN"); break;
      case MODE_WIFI: display.println("Mode: WIFI"); break;
      case MODE_ANIM: display.println("Mode: ANIM"); break;
      default: display.println("Mode"); break;
    }
    display.display();
    delay(250);
  }

  // handle UP/DOWN depending on mode
  if (upPressed) {
    if (currentMode == MODE_TIMER_SET) {
      if (timerSetMinutes < 15) timerSetMinutes++;
      timerRemainingMs = (unsigned long)timerSetMinutes * 60UL * 1000UL;
    } else if (currentMode == MODE_COUNTDOWN) {
      // start/pause toggle
      timerRunning = !timerRunning;
      if (timerRunning) {
        // resume tick basis
        timerLastTickMs = now;
      }
    }
  }
  if (downPressed) {
    if (currentMode == MODE_TIMER_SET) {
      if (timerSetMinutes > 0) timerSetMinutes--;
      timerRemainingMs = (unsigned long)timerSetMinutes * 60UL * 1000UL;
    } else if (currentMode == MODE_COUNTDOWN) {
      // reset to set value and pause
      timerRunning = false;
      timerRemainingMs = (unsigned long)timerSetMinutes * 60UL * 1000UL;
    }
  }

  // Timer countdown (non-blocking)
  if (currentMode == MODE_COUNTDOWN && timerRunning) {
    unsigned long elapsed = now - timerLastTickMs;
    if (elapsed >= 200) { // update every 200ms for smoothness
      // decrement
      if (timerRemainingMs > elapsed) {
        timerRemainingMs -= elapsed;
      } else {
        timerRemainingMs = 0;
      }
      timerLastTickMs = now;

      // when reaches zero
      if (timerRemainingMs == 0) {
        timerRunning = false;
        // alarm
        timeUpAlarm();
        // reset to set value (optional) - we'll keep it at 0 until reset by user
      }
    }
  } else {
    // keep timerLastTickMs fresh when not running
    timerLastTickMs = now;
  }

  // Display updates depending on mode and timings
  if (currentMode == MODE_ANIM) {
    if (now - animLastMs >= ANIM_UPDATE_MS) {
      animLastMs = now;
      // move anim
      animPos += animDir * 3; // speed
      if (animPos < 4) { animPos = 4; animDir = 1; }
      if (animPos > OLED_WIDTH - 4) { animPos = OLED_WIDTH - 4; animDir = -1; }
      drawAnimPage();
    }
  } else {
    // pages that update ~1s
    if (now - lastDisplayUpdate >= CLOCK_UPDATE_MS) {
      lastDisplayUpdate = now;
      switch (currentMode) {
        case MODE_CLOCK: drawClockPage(); break;
        case MODE_TIMER_SET: drawTimerSetPage(); break;
        case MODE_COUNTDOWN: drawCountdownPage(); break;
        case MODE_WIFI: drawWifiPage(); break;
        default: drawClockPage(); break;
      }
    }
  }

  // tiny yield to avoid watchdog issues
  delay(10);
}
