#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#define PIN_FSR A0

// Factor calibrare pentru GRAME (calibrează cu comanda c100 dacă pui 100g, sau c50 pentru 50g)
float factorCalibrare = 1.0;  

int valoareZero = 0;      
float grameFiltrate = 0;
float grameAfisate = -1; // Forțează prima afișare

hd44780_I2Cexp lcd;

void afiseaza(float g) {
  lcd.setCursor(0, 1);
  lcd.print(g, 1);         // Afișează cu o zecimală (ex: 5.5 g)
  lcd.print(" g         ");
}

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
  lcd.begin(16, 2);
  lcd.print("Tarare...");
  lcd.setCursor(0, 1);
  lcd.print("Nu atinge!");

  delay(2000);
  valoareZero = citesteMedie(50); // Citire mai precisă la pornire

  lcd.clear();
  lcd.print("Greutate:");
  afiseaza(0);
  Serial.println("Comenzi: t = tara | c50 = calibrare cu 50 grame pe senzor");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 't') {
      valoareZero = citesteMedie(50);
      grameFiltrate = 0; 
      grameAfisate = 0;
      afiseaza(0);
      Serial.println("Tara facuta");
    } else if (c == 'c') {
      float gRef = Serial.parseFloat(); // Introdu greutatea in GRAME (ex: c50 pentru 50g)
      if (gRef > 0) {
        int citireCurenta = citesteMedie(30) - valoareZero;
        if (citireCurenta > 0) {
          factorCalibrare = (float)citireCurenta / gRef;
          grameFiltrate = gRef;
          Serial.print("Factor nou: ");
          Serial.println(factorCalibrare);
        } else {
          Serial.println("Eroare: Pune greutatea pe senzor inainte de calibrare!");
        }
      }
    }
  }

  // Citim media a 15 eșantioane
  int citireBruta = citesteMedie(15) - valoareZero;
  
  // Filtru de zgomot: ignoră fluctuațiile minuscule de 1-2 unități ADC când e gol
  if (citireBruta <= 2) {
    citireBruta = 0;
  }

  float grame = (float)citireBruta / factorCalibrare;

  // Netezire
  grameFiltrate = 0.85 * grameFiltrate + 0.15 * grame;

  // Dacă valoarea scade sub 1.5 grame, o forțăm la 0.0 ca să nu rămână blocată la 0.05
  if (grameFiltrate < 1.5) {
    grameFiltrate = 0;
  }

  // Actualizează ecranul dacă schimbarea e mai mare de 0.5 grame
  if (fabs(grameFiltrate - grameAfisate) >= 0.5) {
    grameAfisate = round(grameFiltrate * 2) / 2.0; // Rotunjire din 0.5 în 0.5 grame
    afiseaza(grameAfisate);
  }

  Serial.print("Raw: ");
  Serial.print(citireBruta);
  Serial.print(" | Grame: ");
  Serial.println(grameFiltrate, 1);
  
  delay(20);
}
