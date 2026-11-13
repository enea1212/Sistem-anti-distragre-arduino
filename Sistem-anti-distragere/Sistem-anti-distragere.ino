#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#define PIN_FSR A0
#define PIN_LED_ROSU 4
#define PIN_LED_VERDE 5

hd44780_I2Cexp lcd;

int valoareZero = 0;
float grameFiltrate = 0;
float factorCalibrare = 1.0; 

const int PRAG_TELEFON = 50; 
bool telefonPus = false; 

int citesteMedie(int esantioane) {
  long suma = 0;
  for (int i = 0; i < esantioane; i++) {
    suma += analogRead(PIN_FSR);
    delay(2);
  }
  return suma / esantioane;
}

void setup() {
  Serial.begin(9600);
  
  pinMode(PIN_LED_ROSU, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  
  lcd.begin(16, 2);
  
  lcd.print("Calibrare...");
  lcd.setCursor(0, 1);
  lcd.print("Nu atinge!");
  
  digitalWrite(PIN_LED_ROSU, LOW);
  digitalWrite(PIN_LED_VERDE, LOW);
  
  delay(2000);
  valoareZero = citesteMedie(50); 
  
  lcd.clear();
  lcd.print("Pune telefonul");
  digitalWrite(PIN_LED_ROSU, HIGH);  
  digitalWrite(PIN_LED_VERDE, LOW);  
}

void loop() {
  int citireBruta = citesteMedie(15) - valoareZero;
  if (citireBruta <= 2) {
    citireBruta = 0; 
  }
  float grame = (float)citireBruta / factorCalibrare;
  grameFiltrate = 0.85 * grameFiltrate + 0.15 * grame; 
  if (grameFiltrate < 1.5) {
    grameFiltrate = 0;
  }
  
  if (telefonPus == false && grameFiltrate > PRAG_TELEFON) {
    telefonPus = true;
    
    lcd.clear();
    lcd.print("Telefon prezent");
    
    digitalWrite(PIN_LED_ROSU, LOW);   
    digitalWrite(PIN_LED_VERDE, HIGH);
  }
  else if (telefonPus == true && grameFiltrate < PRAG_TELEFON) {
    telefonPus = false;
    
    lcd.clear();
    lcd.print("Pune telefonul");
    
    digitalWrite(PIN_LED_ROSU, HIGH);  
    digitalWrite(PIN_LED_VERDE, LOW); 
  }

  delay(50);
}