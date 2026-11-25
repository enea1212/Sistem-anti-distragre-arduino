#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#define PIN_FSR A0
#define PIN_LED_ROSU 4
#define PIN_LED_VERDE 5
#define PIN_BUZZER 6
#define PIN_BUTON 7   

hd44780_I2Cexp lcd;

int valoareZero = 0; //tara, cand nu se apasa pe senzor
float grameFiltrate = 0;
float factorCalibrare = 1.0; 
bool sesiuneActiva = false; //retine daca am apasat start
int stareAnterioara = -1; //memoreaza ultima stare afisata

// praguri de sensibilitatea alese prin test in serial-monitor
const int PRAG_USOR = 15; // sub 15 - nu avem apasare
//const int PRAG_MEDIU = 150; // intre 15 si 150 avem apasare usoara
//const int PRAG_TARE = 400; // intre 150 si 400 avem apasarea medie iar peste 400 avem apasare foarte puternica

// retine ultima starea a LCD ca sa se actualizeze doar la schimbarea pragului
//int nivelAnterior = -1;

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

  //activeam rezistorul intern pull-up:pinul citeste high cand nu apasam si low cand apasam
  pinMode(PIN_BUTON, INPUT_PULLUP);
  
  lcd.begin(16, 2);
  
  lcd.print("Calibrare...");
  lcd.setCursor(0, 1);
  lcd.print("Nu atinge!");
  
  delay(2000);
  valoareZero = citesteMedie(50); 
  
  lcd.clear();
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

  //cele doua conditii de activare a sesiunii: prezenta telefonului si apasarea butonului
  bool telefonPrezent = (grameFiltrate >= PRAG_USOR);
  bool butonApasat = (digitalRead(PIN_BUTON) == LOW);

  //activam sesiunea de invatat doar cand telefonul este pus si se apasa butonul
  if (telefonPrezent && butonApasat){
    sesiuneActiva = true;
  }

  int stareCurenta = 0;
  if(!telefonPrezent && !sesiuneActiva){
    stareCurenta = 0; //telefonul nu este prezent pe senzorul de presiune
  } else if (telefonPrezent && !sesiuneActiva){
    stareCurenta = 1; //telefonul este pus pe senzor si se asteapta apasarea butonului
  } else if (telefonPrezent && sesiuneActiva) {
    stareCurenta = 2; //sesiunea este activa
  } else if (!telefonPrezent && sesiuneActiva) {
    stareCurenta = 3; //telefonul a fost luat de pe senzor si se activeaza alarma
  }

  //actualizam iesirile LCD, LED, BUZZER doar cand se schimvba starea
  if (stareCurenta != stareAnterioara) {
    stareAnterioara = stareCurenta;
    lcd.setCursor(0, 1);

    if (stareCurenta == 0) {
      //starea 0 - asteptam sa se puna telefonul
      lcd.print("Pune telefonul!!!");
      digitalWrite(PIN_LED_ROSU, HIGH);
      digitalWrite(PIN_LED_VERDE, LOW);
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 1) {
      //starea 1 - am pus telefonul dar nu am apasat start
      lcd.print("Apasa START");
      digitalWrite(PIN_LED_ROSU, LOW);
      digitalWrite(PIN_LED_VERDE, HIGH);
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 2) {
      //starea 2 - sesiune in progres
      lcd.print("Sesiune activa!!!");
      digitalWrite(PIN_LED_ROSU, LOW);
      digitalWrite(PIN_LED_VERDE, HIGH);
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 3) {
      //starea 3 - telefon ridicat dupa pornirea sesiunii, activam buzzer
      lcd.print("PUNE TELEFONUL LA LOC!");
      digitalWrite(PIN_LED_ROSU, HIGH);
      digitalWrite(PIN_LED_VERDE, HIGH);
      tone(PIN_BUZZER, 250);
    }
  }

  delay(50);
}