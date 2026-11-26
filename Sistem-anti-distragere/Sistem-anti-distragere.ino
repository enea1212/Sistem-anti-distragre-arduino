#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#define PIN_FSR A0
#define PIN_BUTON 2        // ATENTIE: pin presupus, buton intre pin 2 si GND (INPUT_PULLUP)
#define PIN_LED_ROSU 4
#define PIN_LED_VERDE 5
#define PIN_BUZZER 6

hd44780_I2Cexp lcd;

int valoareZero = 0;
float grameFiltrate = 0;
float factorCalibrare = 1.0;
bool telefonAFostPus = false;

const int PRAG_USOR = 15;
const int PRAG_MEDIU = 150;
const int PRAG_TARE = 400;

int nivelAnterior = -1;

// ---------- parametri noi ----------
const unsigned long DEBOUNCE_MS = 30;        // filtrare buton
const unsigned long ABANDON_MS = 3000;       // apasare lunga pentru abandon
const unsigned long GRATIE_MS = 3000;        // timp de gratie inainte de alarma
const unsigned long FAZA1_MS = 5000;         // bipuri rare
const unsigned long FAZA2_MS = 10000;        // bipuri dese, dupa aceasta sunet continuu

// ---------- stari ----------
enum Stare { IDLE, SESIUNE };
enum SubStare { NORMAL, GRATIE, ALARMA };

Stare stare = IDLE;
SubStare subStare = NORMAL;

unsigned long tSubStare = 0;   // momentul intrarii in GRATIE / ALARMA
int ultimaSecunda = -1;
bool buzzerPornit = false;
int esecuri = 0;               // sesiuni abandonate (statistici)

// ---------- buton ----------
bool btnUltimaCitire = false;
bool btnStabil = false;        // true = apasat (debounced)
unsigned long btnTimpSchimbare = 0;
unsigned long btnApasatDin = 0;
bool apasareNoua = false;      // se seteaza la fiecare apasare confirmata
bool ignoraPanaLaEliberare = false;

void actualizeazaButon() {
  bool raw = (digitalRead(PIN_BUTON) == LOW);
  if (raw != btnUltimaCitire) {
    btnUltimaCitire = raw;
    btnTimpSchimbare = millis();
  }
  if ((millis() - btnTimpSchimbare) > DEBOUNCE_MS && raw != btnStabil) {
    btnStabil = raw;
    if (btnStabil) {
      btnApasatDin = millis();
      apasareNoua = true;
    }
  }
}

// ---------- utilitare ----------
int citesteMedie(int esantioane) {
  long suma = 0;
  for (int i = 0; i < esantioane; i++) {
    suma += analogRead(PIN_FSR);
    delay(2);
  }
  return suma / esantioane;
}

// scrie un text pe un rand al LCD si sterge restul randului
void scrieLinie(int rand, const char* text) {
  lcd.setCursor(0, rand);
  int n = 0;
  while (text[n] != '\0' && n < 16) {
    lcd.print(text[n]);
    n++;
  }
  for (; n < 16; n++) lcd.print(' ');
}

void setBuzzer(bool pornit) {
  if (pornit == buzzerPornit) return;
  buzzerPornit = pornit;
  if (pornit) tone(PIN_BUZZER, 250);
  else noTone(PIN_BUZZER);
}

void opresteAlarma() {
  setBuzzer(false);
  digitalWrite(PIN_LED_ROSU, LOW);
}

void afiseazaIdle() {
  char buf[17];
  scrieLinie(0, "Apasa butonul");
  snprintf(buf, sizeof(buf), "Esecuri: %d", esecuri);
  scrieLinie(1, buf);
}

void pornesteSesiune() {
  stare = SESIUNE;
  subStare = NORMAL;
  telefonAFostPus = false;
  nivelAnterior = -1;
  ignoraPanaLaEliberare = true;   // apasarea care a pornit sesiunea nu conteaza pentru abandon
  lcd.clear();
  scrieLinie(0, "Sesiune pornita");
  scrieLinie(1, "Pune telefonul");
  Serial.println("Sesiune pornita");
}

void abandoneazaSesiune() {
  opresteAlarma();
  digitalWrite(PIN_LED_VERDE, LOW);
  esecuri++;
  Serial.print("Sesiune abandonata. Esecuri: ");
  Serial.println(esecuri);

  lcd.clear();
  scrieLinie(0, "Sesiune");
  char buf[17];
  snprintf(buf, sizeof(buf), "abandonata (#%d)", esecuri);
  scrieLinie(1, buf);
  delay(2000);

  stare = IDLE;
  subStare = NORMAL;
  apasareNoua = false;
  afiseazaIdle();
}

// ---------- setup ----------
void setup() {
  Serial.begin(9600);

  pinMode(PIN_LED_ROSU, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTON, INPUT_PULLUP);
  digitalWrite(PIN_BUZZER, LOW);

  lcd.begin(16, 2);
  lcd.print("Calibrare...");
  lcd.setCursor(0, 1);
  lcd.print("Nu atinge!");

  delay(2000);
  valoareZero = citesteMedie(50);

  lcd.clear();
  afiseazaIdle();
}

// ---------- loop ----------
void loop() {
  // --- citire senzor (neschimbat) ---
  int citireBruta = citesteMedie(15) - valoareZero;
  if (citireBruta < 0) citireBruta = 0;

  float grame = (float)citireBruta / factorCalibrare;
  grameFiltrate = 0.70 * grameFiltrate + 0.30 * grame;
  if (grameFiltrate < 1.5) grameFiltrate = 0;

  Serial.println(grameFiltrate);

  int nivelCurent = 0;
  if (grameFiltrate < PRAG_USOR) nivelCurent = 0;
  else if (grameFiltrate < PRAG_MEDIU) nivelCurent = 1;
  else if (grameFiltrate < PRAG_TARE) nivelCurent = 2;
  else nivelCurent = 3;

  // --- buton ---
  actualizeazaButon();

  if (stare == IDLE) {
    if (apasareNoua) {
      apasareNoua = false;
      pornesteSesiune();
    }
    delay(20);
    return;
  }

  // ===== stare == SESIUNE =====
  apasareNoua = false;

  // abandon cu apasare lunga
  if (!btnStabil) ignoraPanaLaEliberare = false;
  if (btnStabil && !ignoraPanaLaEliberare && (millis() - btnApasatDin) >= ABANDON_MS) {
    abandoneazaSesiune();
    return;
  }

  if (nivelCurent > 0) {
    // telefonul e pe senzor
    telefonAFostPus = true;

    if (subStare != NORMAL) {      // revenire din gratie sau alarma
      opresteAlarma();
      subStare = NORMAL;
      nivelAnterior = -1;          // fortam reafisarea
    }

    if (nivelCurent != nivelAnterior) {
      nivelAnterior = nivelCurent;
      digitalWrite(PIN_LED_VERDE, HIGH);

      if (nivelCurent == 1) {
        scrieLinie(1, "USOR");
        digitalWrite(PIN_LED_ROSU, LOW);
      } else if (nivelCurent == 2) {
        scrieLinie(1, "MEDIU");
        digitalWrite(PIN_LED_ROSU, LOW);
      } else {
        scrieLinie(1, "FOARTE TARE!");
        digitalWrite(PIN_LED_ROSU, HIGH);
      }
    }
  } else if (telefonAFostPus) {
    // telefonul a fost ridicat
    digitalWrite(PIN_LED_VERDE, LOW);
    nivelAnterior = 0;

    if (subStare == NORMAL) {
      subStare = GRATIE;
      tSubStare = millis();
      ultimaSecunda = -1;
      scrieLinie(0, "ATENTIE!");
    }

    if (subStare == GRATIE) {
      unsigned long scurs = millis() - tSubStare;
      if (scurs >= GRATIE_MS) {
        subStare = ALARMA;
        tSubStare = millis();
        scrieLinie(0, "ALARMA!");
        scrieLinie(1, "PUNE TELEFONUL!!!");
      } else {
        int secunde = 3 - (int)(scurs / 1000);   // 3, 2, 1
        if (secunde != ultimaSecunda) {
          ultimaSecunda = secunde;
          char buf[17];
          snprintf(buf, sizeof(buf), "Pune telefonul %d", secunde);
          scrieLinie(1, buf);
        }
      }
    }

    if (subStare == ALARMA) {
      unsigned long scurs = millis() - tSubStare;

      if (scurs < FAZA1_MS) {
        // bipuri rare: 100 ms pornit la fiecare 1000 ms
        setBuzzer((scurs % 1000) < 100);
        digitalWrite(PIN_LED_ROSU, LOW);
      } else if (scurs < FAZA2_MS) {
        // bipuri dese: 100 ms pornit la fiecare 300 ms
        setBuzzer((scurs % 300) < 100);
        digitalWrite(PIN_LED_ROSU, LOW);
      } else {
        // sunet continuu + LED rosu care clipeste
        setBuzzer(true);
        digitalWrite(PIN_LED_ROSU, ((scurs / 200) % 2) == 0 ? HIGH : LOW);
      }
    }
  }

  delay(20);
}