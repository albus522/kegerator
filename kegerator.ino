#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

#define TEC_CONTROL 13
#define FAN_CONTROL 5

#define FAN_MIN 100
#define FAN_MAX 255

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

#define ONE_WIRE_BUS 6
#define TEMPERATURE_PRECISION 12

OneWire oneWire(ONE_WIRE_BUS);

DallasTemperature sensors(&oneWire);

// arrays to hold device addresses
//DeviceAddress internalTherm, heatsinkTherm;
DeviceAddress internalTherm = { 0x28, 0xF0, 0x5D, 0x67, 0x5, 0x0, 0x0, 0x36 };
DeviceAddress heatsinkTherm = { 0x28, 0x7, 0x7C, 0x68, 0x5, 0x0, 0x0, 0x6E };

#define MAX_DUTY 10000

volatile int setTempF;
volatile int16_t setTempRaw;
volatile unsigned long lastSetTempUpdate;
unsigned long duty = 7500;
unsigned long start;
unsigned long now;
unsigned long cummulativeOff = 0;
unsigned long lastCummulativeOff;
int16_t tempRaw;
int16_t prevTempRaw;
int16_t tempChange;
unsigned long prevTempAt;

int16_t tempFToRaw(int temp) {
  return (temp - 32) * 5 * 128 / 9;
}

void setup(void)
{
  pinMode(TEC_CONTROL, OUTPUT);
  analogWrite(FAN_CONTROL, FAN_MIN);

  digitalWrite(TEC_CONTROL, HIGH);
  start = millis();
  lastCummulativeOff = start;

  setTempF = EEPROM.read(0);
  if(setTempF > 100) setTempF = 42;
  setTempRaw = tempFToRaw(setTempF);
  lastSetTempUpdate = millis();

  lcd.begin(16, 2);
  lcd.print("Set ");
  lcd.print(setTempF);
  lcd.print(" Cur ");
  lcd.setCursor(0, 1);
  lcd.print("Duty % ");
  lcd.print(duty / 100.0);

  // Start up the DallasTemperature library
  sensors.begin();

  // set the resolution
  sensors.setResolution(internalTherm, TEMPERATURE_PRECISION);
  sensors.setResolution(heatsinkTherm, TEMPERATURE_PRECISION);

  sensors.requestTemperatures();
  prevTempRaw = sensors.getTemp(internalTherm);
  prevTempAt = millis();

  attachInterrupt(0, decrementSetTemp, RISING); // Pin 3
  attachInterrupt(1, incrementSetTemp, RISING); // Pin 2
}

void decrementSetTemp()
{
  changeSetTemp(setTempF - 1);
}

void incrementSetTemp()
{
  changeSetTemp(setTempF + 1);
}

void changeSetTemp(int newTemp)
{
  unsigned long now = millis();
  if(now < lastSetTempUpdate || (now - lastSetTempUpdate) > 500) {
    setTempF   = newTemp;
    setTempRaw = tempFToRaw(setTempF);
    EEPROM.write(0, newTemp);
    lcd.setCursor(4, 0);
    lcd.print(setTempF);
    lastSetTempUpdate = now;
  }
}

void loop(void)
{
  unsigned long timeSinceChange;
  sensors.requestTemperatures();

  tempRaw = sensors.getTemp(internalTherm);
  if(tempRaw == DEVICE_DISCONNECTED_RAW) {
    tempRaw = prevTempRaw;
  }
  if(tempRaw != prevTempRaw) {
    tempChange      = tempRaw - prevTempRaw;
    prevTempRaw     = tempRaw;
    prevTempAt      = millis();
    timeSinceChange = 0;
  } else {
    timeSinceChange = millis() - prevTempAt;
  }

  // Refresh because the set temp interrupt
  // could mess this up
  lcd.setCursor(6,0);
  lcd.print(" Cur ");
  lcd.print(int(sensors.rawToFahrenheit(tempRaw) * 100) / 100.0);

  // 4 * 128
  if(tempRaw > (setTempRaw + 512)) {
    duty = MAX_DUTY;
  } else if(tempRaw < setTempRaw) {
    // Temp decreasing or 60 seconds since last change
    if(tempChange < 0 || timeSinceChange > 60000) {
      duty -= (setTempRaw - tempRaw) / 3;
      if(duty > MAX_DUTY || duty < 1000) duty = 1000;
    }
  } else if(tempRaw > setTempRaw) {
    // Temp increasing or 30 seconds since last change
    if(tempChange > 0 || timeSinceChange > 30000) {
      duty += (tempRaw - setTempRaw) / 3;
      if(duty > MAX_DUTY) duty = MAX_DUTY;
    }
  }

  lcd.setCursor(7,1);
  lcd.print("         ");
  lcd.setCursor(7,1);
  lcd.print(duty / 100.0);

  if(duty > (MAX_DUTY * 8 / 10)) {
    analogWrite(FAN_CONTROL, (duty - (MAX_DUTY * 8 / 10)) * (FAN_MAX - FAN_MIN) / (MAX_DUTY * 2 / 10) + FAN_MIN);
  } else {
    analogWrite(FAN_CONTROL, FAN_MIN);
  }

  now = millis();
  delay(constrain(duty - (now - start), 0, MAX_DUTY));

  int off = constrain(MAX_DUTY - duty, 0, MAX_DUTY);
  if(off > 0) {
    digitalWrite(TEC_CONTROL, LOW);
    delay(off);
    cummulativeOff += off;

    digitalWrite(TEC_CONTROL, HIGH);
  }

  start = millis();

  // Check Cummulative Off

  // Has millis rolled over or has an hour passed
  if (start < lastCummulativeOff || (start - lastCummulativeOff) > 3600000) {
    lastCummulativeOff = start;
    // If off less than 60 seconds
    // Give the TEC a break
    if(cummulativeOff < 60000) {
      digitalWrite(TEC_CONTROL, LOW);
      delay(constrain((60000 - cummulativeOff) * 2, 0, 60000));
      digitalWrite(TEC_CONTROL, HIGH);
      start = millis();
    }
    cummulativeOff = 0;
  }
}
