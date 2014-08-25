#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

// Pin config

#define COMPRESSOR_CONTROL 13
#define FAN_CONTROL 5

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);


// Onewire config

#define ONE_WIRE_BUS 6
#define TEMPERATURE_PRECISION 12
#define MAX_CONVERSION_TIME 750

OneWire oneWire(ONE_WIRE_BUS);

DallasTemperature sensors(&oneWire);

// internal device address
DeviceAddress internalTherm = { 0x28, 0xF0, 0x5D, 0x67, 0x5, 0x0, 0x0, 0x36 };


// Global variables

// 1.5°F * (5 / 9 * 128) - 1
#define TEMP_SWING 106
// 3°F * (5 / 9 * 128)
#define MAX_TEMP_SWING 213
// 5°F * (5 / 9 * 128)
#define HIGH_SPEED_CUT_IN 356
// 1.5°F * (5 / 9 * 128)
#define HIGH_SPEED_CUT_OUT 106
// 30 seconds
#define HIGH_SPEED_DELAY 30000
// 3 minutes
#define MIN_OFF_TIME 180000
// Should be about 3000 RPM on BD35F with 101N0220 controller
#define COMPRESSOR_HIGH_SPEED 90

#define COMP_OFF 0
#define COMP_ON 1
#define COMP_HIGH 2
int curState = 0;

volatile int setTempF;
volatile int16_t setTempRaw;
volatile unsigned long lastSetTempUpdate;
unsigned long lastStateChange;
unsigned long nextTempAt;
int16_t tempRaw;
int16_t prevTempRaw;
int16_t lowCutout;


int16_t tempFToRaw(int temp) {
  return (temp - 32) * 5 * 128 / 9;
}

void setup(void)
{
  pinMode(COMPRESSOR_CONTROL, OUTPUT);
  pinMode(FAN_CONTROL, OUTPUT);
  analogWrite(COMPRESSOR_CONTROL, 255);
  analogWrite(FAN_CONTROL, 1);

  // digitalWrite(COMPRESSOR_CONTROL, HIGH);
  lastStateChange = millis();

  setTempF = EEPROM.read(0);
  if(setTempF > 100) setTempF = 42;
  setTempRaw = tempFToRaw(setTempF);
  lastSetTempUpdate = millis();

  lcd.begin(16, 2);
  lcd.print("Set ");
  lcd.print(setTempF);
  lcd.print(" Cur ");
  lcd.setCursor(0, 1);
  lcd.print("State Off");

  // Start up the DallasTemperature library
  sensors.begin();

  // set the resolution
  sensors.setResolution(internalTherm, TEMPERATURE_PRECISION);

  sensors.requestTemperatures();
  prevTempRaw = sensors.getTemp(internalTherm);

  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  nextTempAt = millis() + MAX_CONVERSION_TIME;

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

void setLowCutout()
{
  int16_t swing = tempRaw - setTempRaw;
  if(swing < TEMP_SWING) {
    swing = TEMP_SWING;
  }
  if(swing > MAX_TEMP_SWING) {
    swing = MAX_TEMP_SWING;
  }
  lowCutout = setTempRaw - swing;
}

void reportCompressorStatus()
{
  lcd.setCursor(6,1);
  lcd.print("          ");
  lcd.setCursor(6,1);
  if(curState == COMP_OFF) {
    lcd.print("Off");
  } else if(curState == COMP_ON) {
    lcd.print("On");
  } else if(curState == COMP_HIGH) {
    lcd.print("High");
  } else {
    lcd.print("WTF?");
  }
}

void loop(void)
{
  unsigned long timeSinceChange;
  unsigned long nextStateChange;
  unsigned long now;

  now = millis();
  if(now > nextTempAt || (now < nextTempAt && now > MAX_CONVERSION_TIME)) {
    tempRaw = sensors.getTemp(internalTherm);

    sensors.requestTemperatures();
    nextTempAt = millis() + MAX_CONVERSION_TIME;

    if(tempRaw == DEVICE_DISCONNECTED_RAW) {
      tempRaw = prevTempRaw;
    }
    if(tempRaw != prevTempRaw) {
      prevTempRaw = tempRaw;

      lcd.setCursor(6,0);
      lcd.print(" Cur ");
      lcd.print(int(sensors.rawToFahrenheit(tempRaw) * 100) / 100.0);
    }
  }

  if(curState == COMP_OFF) {
    if(tempRaw > (setTempRaw + TEMP_SWING)) {
      if(now < lastStateChange || (now > MIN_OFF_TIME && (now - MIN_OFF_TIME) > lastStateChange)) {
        lastStateChange = millis();
        // Off is on the compressor
        analogWrite(COMPRESSOR_CONTROL, 0);
        curState = COMP_ON;
        setLowCutout();
        reportCompressorStatus();
        analogWrite(FAN_CONTROL, 175);
      }
    }
  } else if(curState == COMP_ON) {
    if(tempRaw < lowCutout) {
      lastStateChange = millis();
      // On turns the compressor off
      analogWrite(COMPRESSOR_CONTROL, 255);
      curState = COMP_OFF;
      reportCompressorStatus();
      analogWrite(FAN_CONTROL, 1);
    } else if(tempRaw > (setTempRaw + HIGH_SPEED_CUT_IN)) {
      if(now < lastStateChange || (now > HIGH_SPEED_DELAY && (now - HIGH_SPEED_DELAY) > lastStateChange)) {
        lastStateChange = millis();
        analogWrite(COMPRESSOR_CONTROL, COMPRESSOR_HIGH_SPEED);
        curState = COMP_HIGH;
        reportCompressorStatus();
        analogWrite(FAN_CONTROL, 255);
      }
    }
  } else {
    if(tempRaw < (setTempRaw + HIGH_SPEED_CUT_OUT)) {
      lastStateChange = millis();
      // Off is on the compressor
      analogWrite(COMPRESSOR_CONTROL, 0);
      curState = COMP_ON;
      reportCompressorStatus();
    }
  }
}
