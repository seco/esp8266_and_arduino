// 5pin
/* 24l01p    pro mini
   1  gnd   -
   2  vcc   +
   3  ce    9
   4  csn   10
   5  sck   13
   6  mosi  11
   7  miso  12
*/

/*
   17  / int 0    ---> record value to influxdb
   16 / 15 ---> detect range ( m / u / n )
*/

#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/pgmspace.h>
#include <avr/power.h>
#include <SPI.h>
#include <LowPower.h>
#include "nRF24L01.h"
#include "RF24.h"
#include <Wire.h>
//#include "PinChangeInterrupt.h"
#include <Adafruit_ADS1015.h>

#define adc_disable() (ADCSRA &= ~(1<<ADEN)) // disable ADC
#define adc_enable()  (ADCSRA |=  (1<<ADEN)) // re-enable ADC

#define CE_PIN 9
#define CSN_PIN 10

#define DEVICE_ID 15
#define CHANNEL 100

const uint64_t pipes[3] = { 0xFFFFFFFFFFLL, 0xCCCCCCCCCCLL, 0xFFCCFFCCCCLL };

typedef struct {
  uint32_t _salt;
  uint16_t volt;
  int16_t data1;
  int16_t data2;
  uint8_t devid;
} data;

data payload;

RF24 radio(CE_PIN, CSN_PIN);
Adafruit_ADS1115 ads(0x48);

// analogPin 3 / 2 / 1 / int 0
const int wakeupPin = 2;
const int recordPin = 17;
const int nanoPin   = 16;
const int microPin  = 15;
const int ledPin    = 14;

int rangeStatus;
volatile unsigned long counterForloop;
volatile unsigned long counterForloop_old;
volatile boolean checkRangenow;
volatile int noOfWake;

void checkRange() {
  checkRangenow = true;
}

void wakeUp() {
  noOfWake++;
  counterForloop_old = counterForloop;
  checkRangenow = true;
  detachInterrupt(digitalPinToInterrupt(2));
}

void sleepNow() {
  digitalWrite(ledPin, LOW);
  attachInterrupt(digitalPinToInterrupt(2), wakeUp, FALLING);
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
}

void checkRecordBttn() {
  attachInterrupt(digitalPinToInterrupt(2), sleepNow, FALLING);
}

void setup() {
  Serial.begin(115200);

  delay(20);
  adc_disable();

  pinMode(recordPin, INPUT_PULLUP);
  pinMode(wakeupPin, INPUT_PULLUP);
  pinMode(microPin, INPUT_PULLUP);
  pinMode(nanoPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);

  int microStatus = digitalRead(microPin);
  int nanoStatus  = digitalRead(nanoPin);

  rangeStatus = microStatus << 1;
  rangeStatus = rangeStatus + nanoStatus;

  noOfWake = 0;
  counterForloop = 0;
  checkRangenow = false;

  // radio
  radio.begin();
  radio.setChannel(CHANNEL);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  //radio.setAutoAck(1);
  radio.setRetries(15, 15);
  radio.setPayloadSize(11);
  radio.openWritingPipe(pipes[2]);
  radio.stopListening();
  radio.powerDown();

  payload._salt = 0;
  payload.devid = DEVICE_ID;

  ads.setGain(GAIN_ONE);
  ads.begin();

  for (int k = 0; k < 10; k = k + 1) {
    if (k % 2 == 0) {
      digitalWrite(ledPin, HIGH);
    }
    else {
      digitalWrite(ledPin, LOW);
    }
    delay(100);
  }

  sleepNow();
}

void loop() {
  digitalWrite(ledPin, HIGH);

  int microStatus = digitalRead(microPin);
  int nanoStatus  = digitalRead(nanoPin);

  rangeStatus = microStatus << 1;
  rangeStatus = rangeStatus + nanoStatus;

  int16_t results;
  float multiplier = 0.125F;
  results = ads.readADC_Differential_0_1();

  Serial.print("noOfWakeup : ");
  Serial.print(noOfWake);
  Serial.print(" : counterForloop : ");
  Serial.print(counterForloop);
  Serial.print(" : rangeStatus : ");
  Serial.print(rangeStatus);
  Serial.print(" : results : ");
  Serial.print(results * multiplier);
  Serial.print(" mA ----> ");

  if (counterForloop > (counterForloop_old + 10)) {
    checkRecordBttn();
  }

  payload.data1 = results * multiplier ;
  payload.data2 = rangeStatus;
 
  payload._salt = noOfWake ;
  payload.volt = readVcc();

  Serial.print(payload.data1);
  Serial.print(" : data2 : ");
  Serial.println(payload.data2);

  if ( rangeStatus == 1 || rangeStatus == 3 || rangeStatus == 2 ) {
    radioSend();
  }
  
  digitalWrite(ledPin, LOW);
  counterForloop++;
  delay(1000);

}

void radioSend() {
  /*
    radio.powerUp();
    radio.write(&payload , sizeof(payload));
    radio.powerDown();
  */
}

int readVcc() {
  adc_enable();
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
  ADMUX = _BV(MUX5) | _BV(MUX0);
#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
  ADMUX = _BV(MUX3) | _BV(MUX2);
#else
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#endif

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA, ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH
  uint8_t high = ADCH; // unlocks both

  long result = (high << 8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  adc_disable();

  return (int)result; // Vcc in millivolts
}
