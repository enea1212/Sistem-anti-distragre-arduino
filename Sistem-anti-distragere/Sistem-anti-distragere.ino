#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#define PIN_FSR A0
#define PIN_LED_ROSU 4
#define PIN_LED_VERDE 5
#define PIN_BUZZER 6

hd44780_I2Cexp lcd;

int valoareZero = 0; //tara, cand nu se apasa pe senzor
float grameFiltrate = 0;
float factorCalibrare = 1.0; 
bool telefonAFostPus = false;

// praguri de sensibilitatea alese prin test in serial-monitor
const int PRAG_USOR = 15; // sub 15 - nu avem apasare
const int PRAG_MEDIU = 150; // intre 15 si 150 avem apasare usoara
const int PRAG_TARE = 400; // intre 150 si 400 avem apasarea medie iar peste 400 avem apasare foarte puternica

// retine ultima starea a LCD ca sa se actualizeze doar la schimbarea pragului
int nivelAnterior = -1;

//citeste senzorul de mai multe ori pt a elimina fluctuatiile
int citesteMedie(int esantioane) {
  long suma = 0;
  for (int i = 0; i < esantioane; i++) {
    suma += analogRead(PIN_FSR);
    delay(2);
  }
  return suma / esantioane; //media artimetica a citirilor
}

//ruleaza o singura data la pornirea placii
void setup() {
  Serial.begin(9600);
  
  //setam pinii de la led ca iesiri
  pinMode(PIN_LED_ROSU, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);

  //pinul de la buzzer setat ca iesire
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  
  lcd.begin(16, 2);
  
  lcd.print("Calibrare...");
  lcd.setCursor(0, 1);
  lcd.print("Nu atinge!");
  
  delay(2000);
  valoareZero = citesteMedie(50); 
  
  lcd.clear();
  lcd.print("Nivel apasare:");
}

// functia loop ruleaza bucla la infinit
void loop() {

  int citireBruta = citesteMedie(15) - valoareZero; //citestemedia a 15 esantionari si scadem val 0 pt a obtine doar forta
  
  //eliminam val zgomot
  if (citireBruta < 0) {
    citireBruta = 0; 
  }
  

  float grame = (float)citireBruta / factorCalibrare; //convertim citirea in nr cu virgula
  
  grameFiltrate = 0.70 * grameFiltrate + 0.30 * grame; // 0.70 si 0.30 sunt stabilite prin test in serial-monitor
  
  //daca val finala scade aproape de 0, fortam la 0
  if (grameFiltrate < 1.5) {
    grameFiltrate = 0;
  }

  Serial.println(grameFiltrate);

  // nivelul de apasare prezent
  int nivelCurent = 0;
  if (grameFiltrate < PRAG_USOR) {
    nivelCurent = 0; // fara apasare
  } else if (grameFiltrate < PRAG_MEDIU) {
    nivelCurent = 1; // apasat usor
  } else if (grameFiltrate < PRAG_TARE) {
    nivelCurent = 2; // apasat mediu
  } else {
    nivelCurent = 3; // apasat foarte tare
  }

  // ecranul si ledurile le actualizez doar cand se modifica apasarea
  if (nivelCurent != nivelAnterior) {
    nivelAnterior = nivelCurent;
    lcd.setCursor(0, 1);

    if (nivelCurent == 0) {
      digitalWrite(PIN_LED_ROSU, HIGH);
      digitalWrite(PIN_LED_VERDE, LOW);

      if (telefonAFostPus == true) {
        lcd.print("PUNE TELEFONUL!!!");
        tone(PIN_BUZZER, 250);
      } else {
        lcd.print("Nicio apasare ");
      }
    } else {
      telefonAFostPus = true;
      noTone(PIN_BUZZER);

      if (nivelCurent == 1) {
        lcd.print("USOR");
        digitalWrite(PIN_LED_ROSU, LOW);
        digitalWrite(PIN_LED_VERDE, HIGH);
      } else if (nivelCurent == 2) {
          lcd.print("MEDIU");
          digitalWrite(PIN_LED_ROSU, LOW);
          digitalWrite(PIN_LED_VERDE, HIGH);
      } else if (nivelCurent == 3) {
          lcd.print("FOARTE TARE!");
          // aprindem anadoua leduri pt avertizare
          digitalWrite(PIN_LED_ROSU, HIGH); 
          digitalWrite(PIN_LED_VERDE, HIGH);
      }
    }
  }

  delay(50);
}