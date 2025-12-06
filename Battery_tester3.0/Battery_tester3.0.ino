/* ESP32 SSD1306 voltmeter with one-shot multi-sample calibration
   - Divider: R1 = 40k, R2 = 10k => DIVIDER_RATIO = 0.2
   - ADC pin: GPIO36 (ADC1_CH0)
   - Stores calibrated adcReference (in millivolts) in Preferences (NVS)
   - Use Serial to input measured battery voltage when prompted.
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

Preferences prefs;

const int ADC_PIN = 36; // GPIO36 / ADC1_CH0
const float DIVIDER_RATIO = 10.0f / (40.0f + 10.0f); // 0.20

// ADC config
const int ADC_BITS = 12;
const int ADC_MAX = (1 << ADC_BITS) - 1; // 4095

// default fallback adcReference in millivolts (3300 mV)
float adcReference_mV = 3300.0f;

// smoothing
const int SMOOTH_SAMPLES = 8;
float samples[SMOOTH_SAMPLES];
int sampleIdx = 0;
bool samplesFilled = false;

const int RAW_TINY_THRESHOLD = 8;
const float MIN_DISPLAY_VOLTAGE = 0.2f;

const int CAL_SAMPLES = 2000;   // number of raw samples to average during calibration
const int CAL_DELAY_MS = 1;     // delay between samples

// LiFePO4 table
const int lifepo_points = 11;
float lifepo_voltage[lifepo_points] = {10.0,12.0,12.8,12.9,13.0,13.05,13.1,13.2,13.3,13.4,13.6};
int   lifepo_soc[lifepo_points] =       {0,10,20,30,40,50,60,70,80,90,100};
float lifepo_low = 10.3f;

void setup() {
  Serial.begin(115200);
  delay(100);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while(true) delay(10);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,8);
  display.println("Hi there !!!");
  display.display();
  delay(1500);

  // ADC
  analogReadResolution(ADC_BITS);
  analogSetPinAttenuation(ADC_PIN, ADC_11db);

  // init smoothing buffer
  for (int i = 0; i < SMOOTH_SAMPLES; ++i) samples[i] = 0.0f;

  // load stored calibration if exists
  prefs.begin("adc_cal", true); // read-only first to check
  if (prefs.isKey("ref_mV")) {
    prefs.end();
    prefs.begin("adc_cal", false);
    adcReference_mV = prefs.getFloat("ref_mV", 3300.0f);
    Serial.printf("Loaded stored adcReference = %.3f mV\n", adcReference_mV);
  } else {
    prefs.end();
    Serial.println("No stored calibration. Using default 3300 mV. To calibrate, type measured battery voltage (e.g. 13.27) and press Enter.");
  }
  // prompt user to calibrate
  Serial.println("If you want to calibrate now, connect battery and enter measured total battery voltage in Volts, e.g. 13.27");
  Serial.println("Otherwise nothing to do — sketch will run with current adcReference.");
}

float readRaw() {
  return (float)analogRead(ADC_PIN);
}

float readBatteryVoltageFromRaw(int raw) {
  if (raw <= RAW_TINY_THRESHOLD) return 0.0f;
  if (raw >= ADC_MAX) {
    // saturated: estimate using current adcReference_mV (in mV)
    float vout = ((float)raw / (float)ADC_MAX) * (adcReference_mV / 1000.0f);
    return vout / DIVIDER_RATIO;
  }
  float vout = ((float)raw / (float)ADC_MAX) * (adcReference_mV / 1000.0f);
  return vout / DIVIDER_RATIO;
}

float getSmoothedVoltage() {
  float v = readBatteryVoltageFromRaw(analogRead(ADC_PIN));
  samples[sampleIdx++] = v;
  if (sampleIdx >= SMOOTH_SAMPLES) {
    sampleIdx = 0;
    samplesFilled = true;
  }
  float sum = 0.0f;
  int count = samplesFilled ? SMOOTH_SAMPLES : sampleIdx;
  for (int i = 0; i < count; ++i) sum += samples[i];
  float avg = (count>0) ? (sum / (float)count) : v;
  if (avg < MIN_DISPLAY_VOLTAGE) return 0.0f;
  return avg;
}

int voltageToSOC(float v) {
  if (v <= lifepo_voltage[0]) return lifepo_soc[0];
  if (v >= lifepo_voltage[lifepo_points-1]) return lifepo_soc[lifepo_points-1];
  for (int i=0;i<lifepo_points-1;i++) {
    float v1 = lifepo_voltage[i];
    float v2 = lifepo_voltage[i+1];
    if (v >= v1 && v <= v2) {
      int s1 = lifepo_soc[i];
      int s2 = lifepo_soc[i+1];
      if (fabsf(v2 - v1) < 1e-6) return s1;
      float t = (v - v1) / (v2 - v1);
      float socf = s1 + t*(s2 - s1);
      return (int)round(constrain(socf, 0, 100));
    }
  }
  return 0;
}

void doCalibration(float measuredBatteryVoltage) {
  Serial.println("Starting calibration: averaging samples...");
  unsigned long sumRaw = 0;
  int validSamples = 0;
  for (int i = 0; i < CAL_SAMPLES; ++i) {
    int raw = analogRead(ADC_PIN);
    if (raw > RAW_TINY_THRESHOLD && raw < ADC_MAX) {
      sumRaw += raw;
      validSamples++;
    }
    delay(CAL_DELAY_MS);
  }
  if (validSamples == 0) {
    Serial.println("Calibration failed: no valid samples (battery might be disconnected or ADC saturated).");
    return;
  }

  float rawAvg = (float)sumRaw / (float)validSamples;
  // measured ADC pin voltage (Vout) in millivolts
  float measuredVout_mV = measuredBatteryVoltage * DIVIDER_RATIO * 1000.0f;

  // compute adcReference_mV such that rawAvg/ADC_MAX = measuredVout / adcReference
  // => adcReference = measuredVout * ADC_MAX / rawAvg
  adcReference_mV = (measuredVout_mV * (float)ADC_MAX) / rawAvg;

  Serial.printf("Calibration done. samples=%d, rawAvg=%.2f\n", validSamples, rawAvg);
  Serial.printf("Computed adcReference = %.3f mV (will be stored)\n", adcReference_mV);

  // store to NVS
  prefs.begin("adc_cal", false);
  prefs.putFloat("ref_mV", adcReference_mV);
  prefs.end();
  Serial.println("Saved calibration to NVS. Reboot or continue; new calibration will be used now.");
}

void drawBar(int x,int y,int w,int h,int percent) {
  display.drawRect(x,y,w,h,SSD1306_WHITE);
  int innerW = (w-2)*percent/100;
  if (innerW>0) display.fillRect(x+1,y+1,innerW,h-2,SSD1306_WHITE);
}

void loop() {
  // Check for serial input: if user types a number, run calibration
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length() > 0) {
      float meas = s.toFloat();
      if (meas > 5.0f && meas < 30.0f) { // plausible pack volt
        Serial.printf("User entered measured battery voltage: %.3f V -> running calibration...\n", meas);
        doCalibration(meas);
      } else {
        Serial.println("Invalid number. Please enter measured battery voltage like 13.27");
      }
    }
  }

  // Display updated values
  float battV = getSmoothedVoltage();
  int soc = voltageToSOC(battV);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.printf("V: %.2f V", battV);
  display.setCursor(0,10);
  display.printf("SOC: %d%%", soc);
  drawBar(64,6,60,12,soc);
  if (battV < lifepo_low && battV > 0.0f) {
    display.setCursor(0,22);
    display.print("CRITICAL BATTERY LVL!");
  } else {
    display.setCursor(0,22);
    display.print("Battery OK");
  }
  display.display();

  Serial.printf("raw: %d, Vbat: %.3f V, SOC: %d%%, adcRef_mV: %.3f\n", analogRead(ADC_PIN), battV, soc, adcReference_mV);
  delay(600);
}
