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
bool sesiuneTerminata = false; //devine true cand timerul a ajuns la 0
int stareAnterioara = -1; //memoreaza ultima stare afisata

// praguri de sensibilitatea alese prin test in serial-monitor
const int PRAG_USOR = 15; // sub 15 - nu avem apasare
//const int PRAG_MEDIU = 150; // intre 15 si 150 avem apasare usoara
//const int PRAG_TARE = 400; // intre 150 si 400 avem apasarea medie iar peste 400 avem apasare foarte puternica

// ---------------- TIMER ----------------
// durata sesiunii de invatat in minute (se modifica aici)
const unsigned long DURATA_SESIUNE_MIN = 1;
const unsigned long DURATA_SESIUNE_MS = DURATA_SESIUNE_MIN * 60UL * 1000UL;

unsigned long timpRamasMs = 0;      // cat timp mai e pana la finalul sesiunii
unsigned long ultimulMillis = 0;    // momentul ultimei treceri prin loop (pt a calcula cat timp a trecut)
long ultimaSecundaAfisata = -1;     // ca sa rescriem timerul pe LCD doar o data pe secunda

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

// scrie un text pe un rand al LCD-ului si completeaza cu spatii pana la 16 caractere
// (sterge resturile de la mesajul anterior)
void scrieRand(int rand, const char* text) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd.setCursor(0, rand);
  lcd.print(buf);
}

// afiseaza pe randul 0 timpul ramas in format MM:SS
void afiseazaTimer(unsigned long ms) {
  unsigned long totalSec = (ms + 999) / 1000; // rotunjim in sus ca sa nu apara 00:00 inainte de final
  char buf[17];
  snprintf(buf, sizeof(buf), "Timp: %02lu:%02lu", totalSec / 60, totalSec % 60);
  scrieRand(0, buf);
}

// 3 bipuri scurte la finalul sesiunii
void bipFinal() {
  for (int i = 0; i < 3; i++) {
    tone(PIN_BUZZER, 1000, 150);
    delay(250);
  }
  noTone(PIN_BUZZER);
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
  ultimulMillis = millis();
}

// functia loop ruleaza bucla la infinit
void loop() {

  // cat timp a trecut de la ultima trecere prin loop
  unsigned long acum = millis();
  unsigned long trecut = acum - ultimulMillis;
  ultimulMillis = acum;

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
  //la pornire timerul se reseteaza la durata completa
  if (telefonPrezent && butonApasat && !sesiuneActiva){
    sesiuneActiva = true;
    sesiuneTerminata = false;
    timpRamasMs = DURATA_SESIUNE_MS;
    ultimaSecundaAfisata = -1;
  }

  //timerul scade doar cat timp telefonul e pe senzor
  //(daca telefonul e ridicat, timerul sta pe loc - nu poti "pacali" sistemul asteptand cu telefonul in mana)
  if (sesiuneActiva && telefonPrezent) {
    if (trecut >= timpRamasMs) {
      timpRamasMs = 0;
    } else {
      timpRamasMs -= trecut;
    }

    //timerul a ajuns la 0 -> sesiunea s-a terminat, telefonul poate fi luat
    if (timpRamasMs == 0) {
      sesiuneActiva = false;
      sesiuneTerminata = true;
    }
  }

  //dupa ce sesiunea s-a terminat si telefonul a fost luat, revenim la starea initiala
  if (sesiuneTerminata && !telefonPrezent) {
    sesiuneTerminata = false;
  }

  int stareCurenta = 0;
  if (sesiuneTerminata) {
    stareCurenta = 4; //timerul a expirat, telefonul poate fi luat
  } else if(!telefonPrezent && !sesiuneActiva){
    stareCurenta = 0; //telefonul nu este prezent pe senzorul de presiune
  } else if (telefonPrezent && !sesiuneActiva){
    stareCurenta = 1; //telefonul este pus pe senzor si se asteapta apasarea butonului
  } else if (telefonPrezent && sesiuneActiva) {
    stareCurenta = 2; //sesiunea este activa, timerul numara
  } else if (!telefonPrezent && sesiuneActiva) {
    stareCurenta = 3; //telefonul a fost luat inainte de final si se activeaza alarma
  }

  //actualizam iesirile LCD, LED, BUZZER doar cand se schimba starea
  if (stareCurenta != stareAnterioara) {
    stareAnterioara = stareCurenta;

    if (stareCurenta == 0) {
      //starea 0 - asteptam sa se puna telefonul
      scrieRand(0, "");
      scrieRand(1, "Pune telefonul!");
      digitalWrite(PIN_LED_ROSU, HIGH);
      digitalWrite(PIN_LED_VERDE, LOW);
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 1) {
      //starea 1 - am pus telefonul dar nu am apasat start
      char buf[17];
      snprintf(buf, sizeof(buf), "Durata: %lu min", DURATA_SESIUNE_MIN);
      scrieRand(0, buf);
      scrieRand(1, "Apasa START");
      digitalWrite(PIN_LED_ROSU, LOW);
      digitalWrite(PIN_LED_VERDE, HIGH);
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 2) {
      //starea 2 - sesiune in progres
      scrieRand(1, "Sesiune activa");
      ultimaSecundaAfisata = -1; //fortam reafisarea timerului
      digitalWrite(PIN_LED_ROSU, LOW);
      digitalWrite(PIN_LED_VERDE, HIGH);
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 3) {
      //starea 3 - telefon ridicat inainte de finalul timerului, activam buzzer
      ultimaSecundaAfisata = -1;
      scrieRand(1, "PUNE-L LA LOC!");
      digitalWrite(PIN_LED_ROSU, HIGH);
      digitalWrite(PIN_LED_VERDE, LOW);
      tone(PIN_BUZZER, 250);
    }
    else if (stareCurenta == 4) {
      //starea 4 - timerul a expirat, sesiune incheiata
      scrieRand(0, "Sesiune gata!");
      scrieRand(1, "Poti lua telef.");
      digitalWrite(PIN_LED_ROSU, LOW);
      digitalWrite(PIN_LED_VERDE, HIGH);
      bipFinal();
    }
  }

  //in timpul sesiunii (activa sau cu alarma) afisam timpul ramas, o data pe secunda
  if (sesiuneActiva) {
    long secundaCurenta = (timpRamasMs + 999) / 1000;
    if (secundaCurenta != ultimaSecundaAfisata) {
      ultimaSecundaAfisata = secundaCurenta;
      afiseazaTimer(timpRamasMs);
    }
  }

  delay(50);
}
