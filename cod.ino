#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Servo.h>
#include <avr/interrupt.h>
#include <EEPROM.h>

// --- Configurari Hardware ---
LiquidCrystal_I2C lcd(0x27, 16, 2); 
Servo usaSeif;

const byte pinSenzor = 2; // INT0 (Senzor Magnetic)
const byte pinServo = 11;
const byte pinBuzzer = 12;
const byte pinLaser = 13; // Pinul pentru modulul Laser

// Pini LED RGB (Catod Comun)
const byte pinR = A0;
const byte pinG = A1;
const byte pinB = A2;

// --- Configurarea Tastaturii ---
const byte RANDURI = 4; 
const byte COLOANE = 4; 
char taste[RANDURI][COLOANE] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte piniRanduri[RANDURI] = {10, 9, 8, 7}; 
byte piniColoane[COLOANE] = {6, 5, 4, 3};  
Keypad tastatura = Keypad(makeKeymap(taste), piniRanduri, piniColoane, RANDURI, COLOANE);

// --- Masina de Stari (FSM) ---
enum StareSistem { 
  BLOCAT, 
  ASTEPTARE_MAGNET, 
  DEBLOCAT,
  SCHIMBARE_PIN_VECHI,
  SCHIMBARE_PIN_NOU,
  MOD_APARARE // Starea de alarma cu laser
};
StareSistem stareCurenta = BLOCAT; 

// --- Variabile ---
String pinSalvat = ""; 
String pinIntrodus = "";    
volatile bool magnetDetectat = false; 
int incercariGresite = 0; // Contor pentru siguranta

// --- RUTINA DE INTRERUPERE (ISR) ---
ISR(INT0_vect) {
  if (stareCurenta == ASTEPTARE_MAGNET) {
    magnetDetectat = true;
  }
}

void setup() {
  // --- Initializare EEPROM ---
  if (EEPROM.read(0) == 255) {
    EEPROM.update(0, '1');
    EEPROM.update(1, '2');
    EEPROM.update(2, '3');
    EEPROM.update(3, '4');
  }
  
  for (int i = 0; i < 4; i++) {
    pinSalvat += (char)EEPROM.read(i);
  }

  // --- Initializare Periferice ---
  lcd.init();
  lcd.backlight();
  
  usaSeif.attach(pinServo);
  usaSeif.write(90); // STOP absolut pentru servo 360
  
  pinMode(pinBuzzer, OUTPUT);
  pinMode(pinLaser, OUTPUT);
  digitalWrite(pinLaser, LOW); // Laserul oprit la pornire

  pinMode(pinR, OUTPUT);
  pinMode(pinG, OUTPUT);
  pinMode(pinB, OUTPUT);
  
  pinMode(pinSenzor, INPUT_PULLUP);

  // --- CONFIGURARE REGISTRE PENTRU INT0 ---
  cli(); 
  EICRA |= (1 << ISC01);
  EICRA &= ~(1 << ISC00);
  EIMSK |= (1 << INT0);
  sei(); 
  // ----------------------------------------

  afiseazaEcranBlocat();
}

void loop() {
  char tasta = tastatura.getKey();

  switch (stareCurenta) {
    
    case BLOCAT:
      if (tasta) {
        if (tasta == '*') {
          pinIntrodus = "";
          afiseazaEcranBlocat();
        } 
        else if (tasta == '#') {
          verificaPIN();
        } 
        else if (tasta == 'A') {
          stareCurenta = SCHIMBARE_PIN_VECHI;
          pinIntrodus = "";
          seteazaRGB(LOW, LOW, HIGH); 
          lcd.clear();
          lcd.print("PIN-ul Vechi:");
          lcd.setCursor(0, 1);
        }
        else {
          if (pinIntrodus.length() < 16) {
            pinIntrodus += tasta;
            lcd.print('*');
            emiteSunet(50); 
          }
        }
      }
      break;

    case SCHIMBARE_PIN_VECHI:
      if (tasta) {
        if (tasta == '*') {
          stareCurenta = BLOCAT;
          pinIntrodus = "";
          afiseazaEcranBlocat();
        } 
        else if (tasta == '#') {
          if (pinIntrodus == pinSalvat) {
            stareCurenta = SCHIMBARE_PIN_NOU;
            pinIntrodus = "";
            lcd.clear();
            lcd.print("PIN Nou (4 cif):");
            lcd.setCursor(0, 1);
            emiteSunet(200);
          } else {
            lcd.clear();
            lcd.print("PIN Incorect!");
            emiteSunet(500); delay(2000);
            stareCurenta = BLOCAT;
            pinIntrodus = "";
            afiseazaEcranBlocat();
          }
        } 
        else {
          if (pinIntrodus.length() < 16) {
            pinIntrodus += tasta;
            lcd.print('*');
            emiteSunet(50);
          }
        }
      }
      break;

    case SCHIMBARE_PIN_NOU:
      if (tasta) {
        if (tasta == '*') {
          stareCurenta = BLOCAT;
          pinIntrodus = "";
          afiseazaEcranBlocat();
        } 
        else if (tasta == '#') {
          if (pinIntrodus.length() == 4) {
            for (int i = 0; i < 4; i++) {
              EEPROM.update(i, pinIntrodus[i]);
            }
            pinSalvat = pinIntrodus;
            lcd.clear();
            lcd.print("PIN Salvat!");
            emiteSunet(100); delay(50); emiteSunet(100); delay(1500);
            
            stareCurenta = BLOCAT;
            pinIntrodus = "";
            afiseazaEcranBlocat();
          } else {
            lcd.clear();
            lcd.print("Eroare! Doar 4.");
            emiteSunet(500); delay(2000);
            lcd.clear();
            lcd.print("PIN Nou (4 cif):");
            lcd.setCursor(0, 1);
            pinIntrodus = "";
          }
        } 
        else {
          if (pinIntrodus.length() < 4) {
            pinIntrodus += tasta;
            lcd.print(tasta); 
            emiteSunet(50);
          }
        }
      }
      break;

    case ASTEPTARE_MAGNET:
      if (magnetDetectat) {
        magnetDetectat = false; 
        stareCurenta = DEBLOCAT;
        deschideSeif();
      }
      if (tasta == '*') {
        stareCurenta = BLOCAT;
        pinIntrodus = "";
        afiseazaEcranBlocat();
      }
      break;

    case DEBLOCAT:
      delay(5000); 
      
      usaSeif.write(0);   
      delay(500);         
      usaSeif.write(90);  
      
      emiteSunet(100); emiteSunet(100); 
      
      stareCurenta = BLOCAT;
      pinIntrodus = "";
      afiseazaEcranBlocat();
      break;

    case MOD_APARARE:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ALARM INTRUS!");
      lcd.setCursor(0, 1);
      lcd.print("SISTEM BLOCAT");
      
      digitalWrite(pinLaser, HIGH); // PORNIM LASERUL

      // Efect de Sirena (aprox 4 secunde)
      for (int i = 0; i < 10; i++) {
        seteazaRGB(HIGH, LOW, LOW); // Flash Rosu
        tone(pinBuzzer, 1000);      // Frecventa inalta
        delay(200);
        seteazaRGB(LOW, LOW, LOW);  // LED Oprit
        tone(pinBuzzer, 500);       // Frecventa joasa
        delay(200);
      }
      
      noTone(pinBuzzer); // Oprim buzzer-ul complet
      digitalWrite(pinLaser, LOW); // Oprim laserul
      
      incercariGresite = 0; // Resetam numaratoarea
      stareCurenta = BLOCAT;
      pinIntrodus = "";
      afiseazaEcranBlocat();
      break;
  }
}

// --- Functii Auxiliare ---

void seteazaRGB(bool r, bool g, bool b) {
  digitalWrite(pinR, r);
  digitalWrite(pinG, g);
  digitalWrite(pinB, b);
}

void afiseazaEcranBlocat() {
  seteazaRGB(HIGH, LOW, LOW); 
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Introduceti PIN:");
  lcd.setCursor(0, 1);
}

void verificaPIN() {
  lcd.clear();
  lcd.setCursor(0, 0);
  
  if (pinIntrodus == pinSalvat) {
    incercariGresite = 0; 
    stareCurenta = ASTEPTARE_MAGNET;
    magnetDetectat = false; 
    seteazaRGB(LOW, LOW, HIGH); 
    lcd.print("PIN OK. Pas 2:");
    lcd.setCursor(0, 1);
    lcd.print("Scanati Magnet!");
    emiteSunet(200); 
  } else {
    incercariGresite++; 
    
    if (incercariGresite >= 3) {
      stareCurenta = MOD_APARARE;
      pinIntrodus = "";
    } else {
      lcd.print("PIN Incorect!");
      lcd.setCursor(0, 1);
      lcd.print("Incercari: ");
      lcd.print(3 - incercariGresite); 
      emiteSunet(500); 
      delay(2000);
      pinIntrodus = "";
      afiseazaEcranBlocat();
    }
  }
}

void deschideSeif() {
  seteazaRGB(LOW, HIGH, LOW); 
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ACCES APROBAT");
  lcd.setCursor(0, 1);
  lcd.print("Seiful e deschis");
  
  emiteSunet(100); delay(50); emiteSunet(150); delay(50); emiteSunet(300);
  
  usaSeif.write(180); 
  delay(500);         
  usaSeif.write(90);  
}

void emiteSunet(int durata) {
  digitalWrite(pinBuzzer, HIGH);
  delay(durata);
  digitalWrite(pinBuzzer, LOW);
}
