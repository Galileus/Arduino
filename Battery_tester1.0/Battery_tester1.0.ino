#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int ANALOG_PIN = A0;
const float DIVIDER_SCALE = 4.0;

// Set this to the suggested calibration factor (multimeter / Arduino_measurement)
const float CALIB_FACTOR = 0.958706125f; // <- use this value

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

int voltageToPercent(float v) {
  const float V_MIN = 3.00;
  const float V_MAX = 4.20;
  float p = (v - V_MIN) / (V_MAX - V_MIN) * 100.0;
  if (p > 100.0) p = 100.0;
  if (p < 0.0) p = 0.0;
  return (int)p;
}

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.print("Battery tester 1.0");
  delay(800);
  lcd.clear();
}

void loop() {
  long vcc_mv = readVcc_mV();
  float batt = readBatteryVoltage(vcc_mv);
  int pct = voltageToPercent(batt);

  Serial.print("Battery (calibrated): ");
  Serial.print(batt, 3);
  Serial.print(" V  (");
  Serial.print(pct);
  Serial.println("%)");

  lcd.setCursor(0, 0);
  lcd.print("BATT: ");
  lcd.print(batt, 3);
  lcd.print("V   ");

  lcd.setCursor(0, 1);
  lcd.print("SOC: ");
  lcd.print(pct);
  lcd.print("%    ");

  delay(1000);
}
