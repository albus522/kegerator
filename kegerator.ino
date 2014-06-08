#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

#define TEC_CONTROL 13
#define FAN_CONTROL 5
#define INTERNAL_FAN_CONTROL 4

#define FAN_MIN 100
#define FAN_MAX 255

LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

#define ONE_WIRE_BUS 6
#define TEMPERATURE_PRECISION 12
#define MAX_CONVERSION_TIME 750

OneWire oneWire(ONE_WIRE_BUS);

DallasTemperature sensors(&oneWire);

// arrays to hold device addresses
//DeviceAddress internalTherm;
DeviceAddress internalTherm = { 0x28, 0xF0, 0x5D, 0x67, 0x5, 0x0, 0x0, 0x36 };

#define MAX_DUTY 10000

volatile int setTempF;
volatile int16_t setTempRaw;
volatile unsigned long lastSetTempUpdate;
unsigned long duty = MAX_DUTY;
unsigned long lastStateChange;
unsigned long lastCummulativeOff;
unsigned long nextTempAt;
unsigned long lastDutyUpdate;
int16_t tempRaw;
int16_t prevTempRaw;
int16_t tempChange;
unsigned long prevTempAt;
int curState = HIGH;

int16_t tempFToRaw(int temp) {
  return (temp - 32) * 5 * 128 / 9;
}

void setup(void)
{
  pinMode(TEC_CONTROL, OUTPUT);
  analogWrite(FAN_CONTROL, FAN_MIN);
  digitalWrite(INTERNAL_FAN_CONTROL, HIGH);

  analogWrite(TEC_CONTROL, 255);
  lastStateChange = millis();
  lastCummulativeOff = lastStateChange;
  lastDutyUpdate = millis();

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

  sensors.requestTemperatures();
  prevTempRaw = sensors.getTemp(internalTherm);
  prevTempAt = millis();

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

void loop(void)
{
  unsigned long timeSinceChange;
  unsigned long now;

  now = millis();
  if(curState == HIGH && (now > nextTempAt || (now < nextTempAt && now > MAX_CONVERSION_TIME))) {
    tempRaw = sensors.getTemp(internalTherm);

    sensors.requestTemperatures();
    nextTempAt = millis() + MAX_CONVERSION_TIME;

    if(tempRaw == DEVICE_DISCONNECTED_RAW) {
      tempRaw = prevTempRaw;
    }
    if(tempRaw != prevTempRaw) {
      tempChange      = tempRaw - prevTempRaw;
      prevTempRaw     = tempRaw;
      prevTempAt      = millis();
      timeSinceChange = 0;

      // Refresh because the set temp interrupt
      // could mess this up
      lcd.setCursor(6,0);
      lcd.print(" Cur ");
      lcd.print(int(sensors.rawToFahrenheit(tempRaw) * 100) / 100.0);
    } else {
      timeSinceChange = millis() - prevTempAt;
    }

    // 5 * (5 / 9 * 128)
    if(tempRaw > (setTempRaw + 355)) {
      if(duty != MAX_DUTY) lastDutyUpdate = now;
      duty = MAX_DUTY;
    } else if(tempRaw < setTempRaw) {
      // Temp decreasing or 60 seconds since last change
      if((tempChange < 0  || timeSinceChange > 60000) && (now < lastDutyUpdate || (now - lastDutyUpdate) > MAX_DUTY)) {
        duty -= (setTempRaw - tempRaw) / 3;
        if(duty > MAX_DUTY || duty < 1000) duty = 1000;
        lastDutyUpdate = now;
      }
    } else if(tempRaw > setTempRaw) {
      // Temp increasing or 30 seconds since last change
      if((tempChange > 0 || timeSinceChange > 30000) && (now < lastDutyUpdate || (now - lastDutyUpdate) > MAX_DUTY)) {
        duty += (tempRaw - setTempRaw) / 3;
        if(duty > MAX_DUTY) duty = MAX_DUTY;
        lastDutyUpdate = now;
      }
    }

    if(lastDutyUpdate == now) {
      lcd.setCursor(7,1);
      lcd.print("         ");
      lcd.setCursor(7,1);
      lcd.print(duty / 100.0);
    }

    if(duty > (MAX_DUTY * 7 / 10)) {
      analogWrite(FAN_CONTROL, (duty - (MAX_DUTY * 0.7)) * (FAN_MAX - FAN_MIN) / (MAX_DUTY * 0.3) + FAN_MIN);
    } else {
      analogWrite(FAN_CONTROL, FAN_MIN);
    }
  }

  now = millis();
  if(curState == LOW) {
    if(now < lastStateChange || (now - lastStateChange) > ((MAX_DUTY - duty) / 10)) {
      analogWrite(TEC_CONTROL, 254);
      curState = HIGH;
      lastStateChange = millis();
    }
  } else if(duty < MAX_DUTY) {
    if(now < lastStateChange || (now - lastStateChange) > (duty / 10)) {
      analogWrite(TEC_CONTROL, 0);
      curState = LOW;
      lastStateChange = millis();
    }
  }

  // Check Cummulative Off
  now = millis();
  // Has millis rolled over or have 4 hours passed
  if(duty == MAX_DUTY && (now < lastCummulativeOff || (now - lastCummulativeOff) > 14400000)) {
    lastCummulativeOff = now;
    analogWrite(TEC_CONTROL, 16);
    analogWrite(FAN_CONTROL, 0);
    digitalWrite(INTERNAL_FAN_CONTROL, LOW);
    delay(120000);
    analogWrite(TEC_CONTROL, 254);
    digitalWrite(INTERNAL_FAN_CONTROL, HIGH);
    lastStateChange = millis();
  }
}
