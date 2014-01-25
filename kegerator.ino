#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

#define ONE_WIRE_BUS 6
#define TEMPERATURE_PRECISION 12

OneWire oneWire(ONE_WIRE_BUS);

DallasTemperature sensors(&oneWire);

// arrays to hold device addresses
//DeviceAddress internalTherm, heatsinkTherm;
DeviceAddress internalTherm = { 0x28, 0xF0, 0x5D, 0x67, 0x5, 0x0, 0x0, 0x36 };
DeviceAddress heatsinkTherm = { 0x28, 0x7, 0x7C, 0x68, 0x5, 0x0, 0x0, 0x6E };

int tecControl = 7;
int fanControl = 5;

LiquidCrystal lcd(8, 9, 10, 11, 12, 13);

volatile int setTempF;
// float setTempC = (setTempF - 32) * 5 / 9.0;
unsigned long maxDuty = 10000;
unsigned long duty = 5000;
unsigned long start;
unsigned long now;
unsigned long pause;
float tempF;
float prevTempF;

void setup(void)
{
  pinMode(tecControl, OUTPUT);
  analogWrite(fanControl, 100);

  digitalWrite(tecControl, HIGH);
  start = millis();

  setTempF = EEPROM.read(0);
  if(setTempF > 100) setTempF = 45;

  lcd.begin(16, 2);
  lcd.print("Set ");
  lcd.print(setTempF);
  lcd.print(" Cur ");
  lcd.setCursor(0, 1);
  lcd.print("Duty % ");
  lcd.print(duty / 100.0);

  // Start up the library
  sensors.begin();

  // set the resolution to 9 bit
  sensors.setResolution(internalTherm, TEMPERATURE_PRECISION);
  sensors.setResolution(heatsinkTherm, TEMPERATURE_PRECISION);

  attachInterrupt(0, decrementSetTemp, RISING); // Pin 3
  attachInterrupt(1, incrementSetTemp, RISING); // Pin 2

  sensors.requestTemperatures();
  prevTempF = sensors.getTempF(internalTherm);
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
  setTempF = newTemp;
  EEPROM.write(0, newTemp);
  lcd.setCursor(4, 0);
  lcd.print(setTempF);
}

void loop(void)
{
  sensors.requestTemperatures();

  tempF = sensors.getTempF(internalTherm);
  // Refresh because the set temp interrupt
  // could mess this up
  lcd.setCursor(6,0);
  lcd.print(" Cur ");
  lcd.print(int(tempF * 100) / 100.0);
  if(tempF > (setTempF + 4)) {
    duty = maxDuty;
  } else if(tempF < setTempF && tempF <= prevTempF) {
    duty -= 10 * long((setTempF - tempF) * 5);
    if(duty > maxDuty || duty < 1000) duty = 1000;
  } else if(tempF > setTempF && tempF >= prevTempF) {
    duty += 10 * long((tempF - setTempF) * 5);
    if(duty > maxDuty) duty = maxDuty;
  }
  prevTempF = tempF;

  lcd.setCursor(7,1);
  lcd.print("         ");
  lcd.setCursor(7,1);
  lcd.print(duty / 100.0);

  now = millis();
  delay(constrain(duty - (now - start), 1000, maxDuty));

  digitalWrite(tecControl, LOW);
  delay(constrain(maxDuty - duty, 1000, maxDuty));

  digitalWrite(tecControl, HIGH);
  start = millis();
}
