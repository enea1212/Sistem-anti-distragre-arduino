#include <Wire.h>
#include <EEPROM.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#define PIN_FSR A0
#define PIN_LED_ROSU 4
#define PIN_LED_VERDE 5
#define PIN_BUZZER 6
#define PIN_BUTON 7

hd44780_I2Cexp lcd;

// ================= SENZOR =================
int valoareZero = 0; //tara, cand nu se apasa pe senzor
float grameFiltrate = 0;
float factorCalibrare = 1.0;

// praguri de sensibilitatea alese prin test in serial-monitor
const int PRAG_USOR = 15; // sub 15 - nu avem apasare
//const int PRAG_MEDIU = 150; // intre 15 si 150 avem apasare usoara
//const int PRAG_TARE = 400; // intre 150 si 400 avem apasarea medie iar peste 400 avem apasare foarte puternica

// ================= DURATE =================
// MOD_TEST = true -> toate duratele sunt in SECUNDE in loc de minute (pt teste / prezentare)
const bool MOD_TEST = false;
const unsigned long UNITATE_MS = MOD_TEST ? 1000UL : 60000UL;

// optiunile prin care trec apasarile scurte pe buton; 0 = mod Pomodoro
const byte NR_OPTIUNI = 5;
const byte DURATE_MIN[NR_OPTIUNI] = {15, 25, 45, 60, 0};
byte optiuneDurata = 1; // implicit 25 min

// setari Pomodoro
const byte POMO_LUCRU = 25;
const byte POMO_PAUZA = 5;
const byte POMO_PAUZA_LUNGA = 15;
const byte POMO_CICLURI = 4; // dupa 4 intervale de lucru vine pauza lunga

// ================= BUTON =================
const unsigned long DEBOUNCE_MS = 30;
const unsigned long PRAG_LUNG_MS = 1000;        // apasare lunga: porneste sesiunea
const unsigned long PRAG_FOARTE_LUNG_MS = 3000; // apasare foarte lunga: abandon sesiune

enum EvButon { EV_NIMIC, EV_SCURT, EV_LUNG, EV_FOARTE_LUNG };

bool ultimaCitireButon = false;
bool butonStabil = false;
unsigned long momentSchimbareButon = 0;
unsigned long butonApasatDeLa = 0;
bool foarteLungTrimis = false;

// ================= STARE SISTEM =================
enum Faza { FAZA_NIMIC, FAZA_LUCRU, FAZA_PAUZA, FAZA_GATA };
Faza faza = FAZA_NIMIC;

bool modPomodoro = false;
byte cicluPomodoro = 0; // cate intervale de lucru s-au terminat in sesiunea Pomodoro curenta
bool pauzaLunga = false;

unsigned long durataFazaMs = 0;   // durata totala a fazei curente (pt bara de progres)
unsigned long timpRamasMs = 0;    // cat mai e din faza curenta
unsigned long ultimulMillis = 0;  // pt a calcula cat timp a trecut intre doua loop-uri
long ultimaSecundaAfisata = -1;   // timerul se rescrie pe LCD doar o data pe secunda
int stareAnterioara = -1;         // ultima stare afisata
bool telefonAnterior = false;     // pt a detecta momentul in care telefonul e ridicat
unsigned long studiuNesalvatMs = 0;

// ================= STATISTICI (EEPROM) =================
const uint16_t MAGIC_STAT = 0x5A17; // daca nu gasim valoarea asta in EEPROM, statisticile nu au fost initializate
const int ADRESA_STAT = 0;

struct Statistici {
  uint16_t magic;
  uint16_t terminate;     // sesiuni (intervale de lucru) duse pana la capat
  uint16_t abandonate;    // sesiuni oprite cu apasare foarte lunga
  uint16_t ridicari;      // de cate ori a fost ridicat telefonul in timpul lucrului
  uint32_t secundeStudiu; // timp total de studiu cu telefonul pe senzor
};
Statistici stat;

void salveazaStatistici() {
  EEPROM.put(ADRESA_STAT, stat); // put scrie doar octetii care s-au schimbat
}

void resetStatistici() {
  stat.magic = MAGIC_STAT;
  stat.terminate = 0;
  stat.abandonate = 0;
  stat.ridicari = 0;
  stat.secundeStudiu = 0;
  salveazaStatistici();
}

void incarcaStatistici() {
  EEPROM.get(ADRESA_STAT, stat);
  if (stat.magic != MAGIC_STAT) {
    resetStatistici(); // prima rulare: EEPROM-ul contine valori aleatoare
  }
}

// muta timpul de studiu acumulat in statistici (secunde intregi)
void adaugaStudiu() {
  stat.secundeStudiu += studiuNesalvatMs / 1000;
  studiuNesalvatMs %= 1000;
}

// ================= FUNCTII AJUTATOARE =================

//citeste senzorul de mai multe ori pt a elimina fluctuatiile
int citesteMedie(int esantioane) {
  long suma = 0;
  for (int i = 0; i < esantioane; i++) {
    suma += analogRead(PIN_FSR);
    delay(2);
  }
  return suma / esantioane; //media artimetica a citirilor
}

// citeste butonul cu debounce si intoarce evenimentul:
// EV_SCURT (eliberat sub 1s), EV_LUNG (eliberat intre 1s si 3s),
// EV_FOARTE_LUNG (trimis imediat ce butonul a fost tinut 3s)
EvButon citesteButon() {
  bool citire = (digitalRead(PIN_BUTON) == LOW);
  unsigned long acum = millis();

  if (citire != ultimaCitireButon) {
    ultimaCitireButon = citire;
    momentSchimbareButon = acum;
  }

  // acceptam schimbarea doar daca citirea a stat la fel DEBOUNCE_MS
  if (acum - momentSchimbareButon >= DEBOUNCE_MS && citire != butonStabil) {
    butonStabil = citire;
    if (butonStabil) {
      butonApasatDeLa = acum;
      foarteLungTrimis = false;
    } else if (!foarteLungTrimis) {
      unsigned long durata = acum - butonApasatDeLa;
      return (durata >= PRAG_LUNG_MS) ? EV_LUNG : EV_SCURT;
    }
  }

  if (butonStabil && !foarteLungTrimis && acum - butonApasatDeLa >= PRAG_FOARTE_LUNG_MS) {
    foarteLungTrimis = true;
    return EV_FOARTE_LUNG;
  }

  return EV_NIMIC;
}

// scrie un text pe un rand al LCD-ului si completeaza cu spatii pana la 16 caractere
void scrieRand(int rand, const char* text) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd.setCursor(0, rand);
  lcd.print(buf);
}

// caractere custom pt bara de progres: caracterul i are i coloane umplute (0..5)
void creeazaCaractereBara() {
  for (byte i = 0; i <= 5; i++) {
    byte model = (0x1F << (5 - i)) & 0x1F;
    byte caracter[8];
    for (byte r = 0; r < 8; r++) {
      caracter[r] = model;
    }
    lcd.createChar(i, caracter);
  }
}

// randul 0: "MM:SS " + bara de progres de 10 caractere (50 de pixeli)
void afiseazaTimerSiBara() {
  unsigned long totalSec = (timpRamasMs + 999) / 1000; // rotunjim in sus
  char buf[8];
  snprintf(buf, sizeof(buf), "%02lu:%02lu ", (totalSec / 60) % 100, totalSec % 60);
  lcd.setCursor(0, 0);
  lcd.print(buf);

  unsigned long trecut = durataFazaMs - timpRamasMs;
  int pixeli = (durataFazaMs > 0) ? (int)(trecut * 50UL / durataFazaMs) : 50;
  for (int celula = 0; celula < 10; celula++) {
    int p = pixeli - celula * 5;
    if (p < 0) p = 0;
    if (p > 5) p = 5;
    lcd.write((uint8_t)p);
  }
}

// 3 bipuri scurte la finalul unei faze
void bipFinal() {
  for (int i = 0; i < 3; i++) {
    tone(PIN_BUZZER, 1000, 150);
    delay(250);
  }
  noTone(PIN_BUZZER);
}

// afiseaza un mesaj pe ambele randuri pentru cateva secunde
void mesajTemporar(const char* r0, const char* r1, unsigned long ms) {
  scrieRand(0, r0);
  scrieRand(1, r1);
  delay(ms);
  ultimulMillis = millis(); // nu numaram pauza asta ca timp trecut
}

// porneste o faza (lucru sau pauza) cu durata data in unitati (minute / secunde in MOD_TEST)
void pornesteFaza(Faza f, byte unitati) {
  faza = f;
  durataFazaMs = (unsigned long)unitati * UNITATE_MS;
  timpRamasMs = durataFazaMs;
  ultimaSecundaAfisata = -1;
  stareAnterioara = -1; // fortam redesenarea LCD-ului
}

void pornesteSesiune() {
  modPomodoro = (DURATE_MIN[optiuneDurata] == 0);
  cicluPomodoro = 0;
  pornesteFaza(FAZA_LUCRU, modPomodoro ? POMO_LUCRU : DURATE_MIN[optiuneDurata]);
}

// timerul de lucru a ajuns la 0
void finalLucru() {
  adaugaStudiu();
  stat.terminate++;
  salveazaStatistici();

  if (modPomodoro) {
    cicluPomodoro++;
    pauzaLunga = (cicluPomodoro % POMO_CICLURI == 0);
    pornesteFaza(FAZA_PAUZA, pauzaLunga ? POMO_PAUZA_LUNGA : POMO_PAUZA);
  } else {
    faza = FAZA_GATA;
    stareAnterioara = -1;
  }
  bipFinal();
}

void abandoneazaSesiune() {
  noTone(PIN_BUZZER);
  adaugaStudiu();
  stat.abandonate++;
  salveazaStatistici();
  faza = FAZA_NIMIC;
  stareAnterioara = -1;
  tone(PIN_BUZZER, 150, 600);
  mesajTemporar("Sesiune", "abandonata", 1500);
}

// ================= MOD STATISTICI (buton tinut apasat la pornire) =================
void afiseazaPaginaStat(byte pagina) {
  char r0[17], r1[17];
  if (pagina == 0) {
    snprintf(r0, sizeof(r0), "Terminate: %u", (unsigned)stat.terminate);
    snprintf(r1, sizeof(r1), "Abandonate: %u", (unsigned)stat.abandonate);
  } else if (pagina == 1) {
    unsigned long minute = stat.secundeStudiu / 60;
    snprintf(r0, sizeof(r0), "Ridicari: %u", (unsigned)stat.ridicari);
    snprintf(r1, sizeof(r1), "Studiu: %luh%02lum", minute / 60, minute % 60);
  } else {
    snprintf(r0, sizeof(r0), "Reset statistici");
    snprintf(r1, sizeof(r1), "tine apasat 3s");
  }
  scrieRand(0, r0);
  scrieRand(1, r1);
}

// scurt = pagina urmatoare, lung = iesire, foarte lung pe pagina 3 = reset
void modStatistici() {
  scrieRand(0, "Statistici");
  scrieRand(1, "Elibereaza buton");
  while (digitalRead(PIN_BUTON) == LOW) {
    delay(10);
  }
  delay(50);

  byte pagina = 0;
  bool redesenare = true;
  while (true) {
    if (redesenare) {
      afiseazaPaginaStat(pagina);
      redesenare = false;
    }

    EvButon ev = citesteButon();
    if (ev == EV_SCURT) {
      pagina = (pagina + 1) % 3;
      redesenare = true;
    } else if (ev == EV_LUNG) {
      return;
    } else if (ev == EV_FOARTE_LUNG) {
      if (pagina == 2) {
        resetStatistici();
        scrieRand(0, "Statistici");
        scrieRand(1, "resetate!");
        delay(1500);
        redesenare = true;
      } else {
        return;
      }
    }
    delay(20);
  }
}

// ================= SETUP =================
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
  creeazaCaractereBara();
  incarcaStatistici();

  // butonul tinut apasat la pornire -> afisam statisticile
  if (digitalRead(PIN_BUTON) == LOW) {
    modStatistici();
  }

  lcd.clear();
  lcd.print("Calibrare...");
  lcd.setCursor(0, 1);
  lcd.print("Nu atinge!");

  delay(2000);
  valoareZero = citesteMedie(50);

  lcd.clear();
  ultimulMillis = millis();
}

// ================= LOOP =================
// functia loop ruleaza bucla la infinit
void loop() {

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

  bool telefonPrezent = (grameFiltrate >= PRAG_USOR);
  EvButon ev = citesteButon();

  switch (faza) {
    case FAZA_NIMIC:
      if (telefonPrezent) {
        //ca inainte: telefonul pus + butonul apasat -> sesiunea porneste imediat
        //(!foarteLungTrimis = apasarea nu a fost deja folosita, ex. la abandon)
        if (butonStabil && !foarteLungTrimis) {
          foarteLungTrimis = true; // ignoram restul acestei apasari (sa nu abandoneze daca tinem mult)
          pornesteSesiune();
        }
      } else if (ev == EV_SCURT) {
        //fara telefon pe senzor: apasarile scurte schimba durata
        optiuneDurata = (optiuneDurata + 1) % NR_OPTIUNI;
        stareAnterioara = -1; // redesenam durata aleasa
      }
      break;

    case FAZA_LUCRU:
      if (ev == EV_FOARTE_LUNG) {
        abandoneazaSesiune();
        break;
      }
      if (telefonPrezent) {
        //timerul scade doar cat timp telefonul e pe senzor
        unsigned long dt = (trecut < timpRamasMs) ? trecut : timpRamasMs;
        timpRamasMs -= dt;
        studiuNesalvatMs += dt;
        if (timpRamasMs == 0) {
          finalLucru();
        }
      } else if (telefonAnterior) {
        // telefonul tocmai a fost ridicat
        stat.ridicari++;
        salveazaStatistici();
      }
      break;

    case FAZA_PAUZA:
      if (ev == EV_FOARTE_LUNG) {
        faza = FAZA_NIMIC;
        stareAnterioara = -1;
        mesajTemporar("Pomodoro", "oprit", 1500);
        break;
      }
      // in pauza timpul curge indiferent unde e telefonul
      timpRamasMs = (trecut < timpRamasMs) ? timpRamasMs - trecut : 0;
      if (timpRamasMs == 0) {
        // pauza s-a terminat -> incepe automat urmatorul interval de lucru
        pornesteFaza(FAZA_LUCRU, POMO_LUCRU);
        bipFinal();
      }
      break;

    case FAZA_GATA:
      // dupa ce sesiunea s-a terminat si telefonul a fost luat, revenim la starea initiala
      if (!telefonPrezent) {
        faza = FAZA_NIMIC;
      }
      break;
  }

  telefonAnterior = telefonPrezent;

  int stareCurenta = 0;
  if (faza == FAZA_NIMIC) {
    stareCurenta = telefonPrezent ? 1 : 0; // 0 - fara telefon, 1 - asteptam START
  } else if (faza == FAZA_LUCRU) {
    stareCurenta = telefonPrezent ? 2 : 3; // 2 - sesiune activa, 3 - alarma
  } else if (faza == FAZA_GATA) {
    stareCurenta = 4; // sesiune terminata, telefonul poate fi luat
  } else {
    stareCurenta = 5; // pauza Pomodoro, telefonul poate fi luat
  }

  //actualizam iesirile LCD, LED, BUZZER doar cand se schimba starea
  if (stareCurenta != stareAnterioara) {
    stareAnterioara = stareCurenta;
    ultimaSecundaAfisata = -1;

    if (stareCurenta == 0 || stareCurenta == 1) {
      //randul 0: durata aleasa (se schimba cu apasari scurte cat telefonul NU e pus)
      char buf[17];
      if (DURATE_MIN[optiuneDurata] == 0) {
        snprintf(buf, sizeof(buf), "Mod: Pomodoro");
      } else {
        snprintf(buf, sizeof(buf), "Durata: %u %s", DURATE_MIN[optiuneDurata], MOD_TEST ? "sec" : "min");
      }
      scrieRand(0, buf);
    }

    if (stareCurenta == 0) {
      //starea 0 - asteptam sa se puna telefonul
      scrieRand(1, "Pune telefonul!");
      digitalWrite(PIN_LED_ROSU, HIGH);
      digitalWrite(PIN_LED_VERDE, LOW);
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 1) {
      //starea 1 - telefon pus, asteptam apasarea butonului
      scrieRand(1, "Apasa START");
      digitalWrite(PIN_LED_ROSU, LOW);
      digitalWrite(PIN_LED_VERDE, HIGH);
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 2) {
      //starea 2 - sesiune in progres
      if (modPomodoro) {
        char buf[17];
        snprintf(buf, sizeof(buf), "Lucru %u/%u", (unsigned)(cicluPomodoro % POMO_CICLURI + 1), (unsigned)POMO_CICLURI);
        scrieRand(1, buf);
      } else {
        scrieRand(1, "Sesiune activa");
      }
      digitalWrite(PIN_LED_ROSU, LOW);
      digitalWrite(PIN_LED_VERDE, HIGH);
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 3) {
      //starea 3 - telefon ridicat inainte de finalul timerului, activam buzzer
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
      noTone(PIN_BUZZER);
    }
    else if (stareCurenta == 5) {
      //starea 5 - pauza Pomodoro, telefonul poate fi luat
      scrieRand(1, pauzaLunga ? "Pauza lunga" : "Pauza");
      digitalWrite(PIN_LED_ROSU, LOW);
      digitalWrite(PIN_LED_VERDE, LOW);
      noTone(PIN_BUZZER);
    }
  }

  // timer + bara de progres, rescrise o data pe secunda
  if (faza == FAZA_LUCRU || faza == FAZA_PAUZA) {
    long secundaCurenta = (timpRamasMs + 999) / 1000;
    if (secundaCurenta != ultimaSecundaAfisata) {
      ultimaSecundaAfisata = secundaCurenta;
      afiseazaTimerSiBara();
    }
  }

  // LED-ul verde clipeste in ultimul minut al sesiunii de lucru
  if (stareCurenta == 2 && timpRamasMs <= UNITATE_MS) {
    digitalWrite(PIN_LED_VERDE, (acum / 500) % 2);
  }

  delay(50);
}
