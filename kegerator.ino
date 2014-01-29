#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

#define TEC_CONTROL 7
#define FAN_CONTROL 5

#define FAN_MIN 100
#define FAN_MAX 255

LiquidCrystal lcd(8, 9, 10, 11, 12, 13);

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
volatile unsigned long lastSetTempUpdate;
unsigned long duty = 7500;
unsigned long start;
unsigned long now;
float tempF;
float prevTempF;
float tempChange;
unsigned long prevTempAt;

void setup(void)
{
  pinMode(TEC_CONTROL, OUTPUT);
  analogWrite(FAN_CONTROL, FAN_MIN);

  digitalWrite(TEC_CONTROL, HIGH);
  start = millis();

  setTempF = EEPROM.read(0);
  if(setTempF > 100) setTempF = 42;
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
  prevTempF = sensors.getTempF(internalTherm);
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
    setTempF = newTemp;
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

  tempF = sensors.getTempF(internalTherm);
  if(tempF != prevTempF) {
    tempChange      = tempF - prevTempF;
    prevTempF       = tempF;
    prevTempAt      = millis();
    timeSinceChange = 0;
  } else {
    timeSinceChange = millis() - prevTempAt;
  }

  // Refresh because the set temp interrupt
  // could mess this up
  lcd.setCursor(6,0);
  lcd.print(" Cur ");
  lcd.print(int(tempF * 100) / 100.0);

  if(tempF > (setTempF + 4)) {
    duty = MAX_DUTY;
  } else if(tempF < setTempF) {
    // Temp decreasing or 60 seconds since last change
    if(tempChange < 0 || timeSinceChange > 60000) {
      duty -= long((setTempF - tempF) * 50);
      if(duty > MAX_DUTY || duty < 1000) duty = 1000;
    }
  } else if(tempF > setTempF) {
    // Temp increasing or 30 seconds since last change
    if(tempChange > 0 || timeSinceChange > 30000) {
      duty += long((tempF - setTempF) * 50);
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

  digitalWrite(TEC_CONTROL, LOW);
  delay(constrain(MAX_DUTY - duty, 1000, MAX_DUTY));

  digitalWrite(TEC_CONTROL, HIGH);
  start = millis();
}
