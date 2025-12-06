#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int ANALOG_PIN = A0;
const float DIVIDER_SCALE = 4.0;
const float CALIB_FACTOR = 0.958706f;

const int BUTTON_PIN = 8;
const int BUZZER_PIN = 9;

int mode = 0; // 0=3S Li-ion, 1=3S LiFePO4, 2=1S Li-ion
bool lastButtonState = HIGH;

// 1S Li-ion
const int li1s_points = 5;
float li1s_voltage[li1s_points] = {3.0, 3.4, 3.7, 3.95, 4.2};
int li1s_soc[li1s_points] = {0,10,50,75,100};
float li1s_low = 3.2;

// 3S Li-ion
const int li_ion_points = 5;
float li_ion_voltage[li_ion_points] = {9.0, 10.5, 11.4, 12.0, 12.6};
int li_ion_soc[li_ion_points] = {0,25,50,75,100};
float li_ion_low = 9.5;

// 4S LiFePO4
const int lifepo_points = 12;
float lifepo_voltage[lifepo_points] = {10.0, 12.0, 12.8, 12.9, 13, 13.05, 13.1, 13.2, 13.3, 13.4, 13.6};
int lifepo_soc[lifepo_points] = {0,10,20,30,40,50,60,70,80,90,100};
float lifepo_low = 10.5;



long readVcc_mV() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC));
  long result = ADCL;
  result |= ADCH << 8;
  result = 1125300L / result;
  return result;
}

float readBatteryVoltage(float vcc_mv) {
  int raw = analogRead(ANALOG_PIN);
  float vout = raw * (vcc_mv / 1000.0) / 1023.0;
  float vin = vout * DIVIDER_SCALE;
  vin *= CALIB_FACTOR;
  return vin;
}

// SOC interpolation using tables
int voltageToSOC(float v, int mode) {
  float *volt;
  int *soc;
  int points;

  if (mode == 0) {
    volt = li_ion_voltage;
    soc = li_ion_soc;
    points = li_ion_points;
  } else if (mode == 1) {
    volt = lifepo_voltage;
    soc = lifepo_soc;
    points = lifepo_points;
  } else {
    volt = li1s_voltage;
    soc = li1s_soc;
    points = li1s_points;
  }

  if (v <= volt[0]) return soc[0];
  if (v >= volt[points-1]) return soc[points-1];

  for (int i=0; i<points-1; i++) {
    if (v >= volt[i] && v <= volt[i+1]) {
      float ratio = (v - volt[i]) / (volt[i+1] - volt[i]);
      return soc[i] + ratio * (soc[i+1] - soc[i]);
    }
  }
  return 0;
}

float getLowVoltage(int mode){
  if (mode == 0) return li_ion_low;
  if (mode == 1) return lifepo_low;
  return li1s_low;
}

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.print("Battery Monitor");
  delay(1000);
  lcd.clear();
}

void loop() {
  bool buttonState = digitalRead(BUTTON_PIN);

  // Button pressed → change mode
  if (lastButtonState == HIGH && buttonState == LOW) {
    mode = (mode + 1) % 3; // now 3 modes
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Mode changed:");
    lcd.setCursor(0, 1);

    if (mode == 0) lcd.print("Li-ion 3S");
    if (mode == 1) lcd.print("LiFePO4 4S");
    if (mode == 2) lcd.print("Li-ion 1S");

    delay(1500);
    lcd.clear();
  }

  lastButtonState = buttonState;

  long vcc_mv = readVcc_mV();
  float batt = readBatteryVoltage(vcc_mv);
  int pct = voltageToSOC(batt, mode);

  // Low voltage buzzer
  if (batt < getLowVoltage(mode)) digitalWrite(BUZZER_PIN, HIGH);
  else digitalWrite(BUZZER_PIN, LOW);

  // Serial output
  Serial.print("Mode: ");
  if (mode == 0) Serial.print("Li-ion 3S");
  if (mode == 1) Serial.print("LiFePO4 4S");
  if (mode == 2) Serial.print("Li-ion 1S");

  Serial.print(" | Vbat: ");
  Serial.print(batt, 3);
  Serial.print(" | SOC: ");
  Serial.print(pct);
  Serial.println("%");

  // LCD output
  lcd.setCursor(0, 0);
  lcd.print("Bat: ");
  lcd.print(batt, 3);
  lcd.print("V   ");

  lcd.setCursor(0, 1);
  lcd.print("SOC: ");
  lcd.print(pct);
  lcd.print("%    ");

  delay(800);
}
