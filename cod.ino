#include <Wire.h>               // Biblioteca pentru comunicarea I2C (folosită de display)
#include <LiquidCrystal_I2C.h>  // Biblioteca pentru controlul display-ului LCD prin I2C
#include <Keypad.h>             // Biblioteca pentru citirea tastaturii matriciale
#include <Servo.h>              // Biblioteca pentru controlul servomotorului
#include <avr/interrupt.h>      // Biblioteca specifică AVR pentru manipularea întreruperilor hardware
#include <EEPROM.h>             // Biblioteca pentru stocarea permanentă a PIN-ului în memorie

// --- Configurari Hardware ---
LiquidCrystal_I2C lcd(0x27, 16, 2); // Inițializare LCD cu adresa I2C 0x27, 16 coloane și 2 rânduri
Servo usaSeif;                      // Crearea obiectului pentru controlul servomotorului

const byte pinSenzor = 2; // Pin digital 2 pentru INT0 (Întrerupere hardware pentru Senzor Magnetic)
const byte pinServo = 11; // Pin digital 11 cu suport PWM pentru semnalul servomotorului
const byte pinBuzzer = 12; // Pin digital 12 pentru controlul difuzorului (buzzer)
const byte pinLaser = 13; // Pinul 13 pentru controlul modulului Laser (folosit la alarmă)

// Pini LED RGB (Catod Comun)
const byte pinR = A0; // Pin analogic folosit ca digital pentru culoarea Roșu
const byte pinG = A1; // Pin analogic folosit ca digital pentru culoarea Verde
const byte pinB = A2; // Pin analogic folosit ca digital pentru culoarea Albastru

// --- Configurarea Tastaturii ---
const byte RANDURI = 4; // Tastatura are 4 rânduri
const byte COLOANE = 4; // Tastatura are 4 coloane
char taste[RANDURI][COLOANE] = {
  {'1','2','3','A'},    // Maparea caracterelor de pe tastatură - Rândul 1
  {'4','5','6','B'},    // Maparea caracterelor de pe tastatură - Rândul 2
  {'7','8','9','C'},    // Maparea caracterelor de pe tastatură - Rândul 3
  {'*','0','#','D'}     // Maparea caracterelor de pe tastatură - Rândul 4
};
byte piniRanduri[RANDURI] = {10, 9, 8, 7}; // Pinii conectați la rândurile R1, R2, R3, R4
byte piniColoane[COLOANE] = {6, 5, 4, 3};  // Pinii conectați la coloanele C1, C2, C3, C4
// Inițializarea obiectului tastatură pe baza mapării și a pinilor declarați
Keypad tastatura = Keypad(makeKeymap(taste), piniRanduri, piniColoane, RANDURI, COLOANE);

// --- Masina de Stari (FSM - Finite State Machine) ---
enum StareSistem { 
  BLOCAT,               // Starea implicită, seiful este închis și așteaptă PIN-ul
  ASTEPTARE_MAGNET,     // PIN-ul a fost corect, se așteaptă validarea cu senzorul magnetic (2FA)
  DEBLOCAT,             // Acces permis, servomotorul deschide seiful
  SCHIMBARE_PIN_VECHI,  // Starea de introducere a PIN-ului curent pentru verificare înainte de modificare
  SCHIMBARE_PIN_NOU,    // Starea în care se introduce noul PIN
  MOD_APARARE           // Starea de alarmă declanșată de prea multe încercări greșite
};
StareSistem stareCurenta = BLOCAT; // Setăm starea inițială la pornirea sistemului

// --- Variabile ---
String pinSalvat = "";     // Variabilă pentru a stoca PIN-ul citit din memoria EEPROM
String pinIntrodus = "";   // Variabilă în care se construiește PIN-ul tastat de utilizator
volatile bool magnetDetectat = false; // Variabilă modificată în interiorul ISR; trebuie declarată 'volatile'
int incercariGresite = 0;  // Contor pentru a bloca sistemul după 3 încercări eșuate

// --- RUTINA DE INTRERUPERE (ISR - Interrupt Service Routine) ---
// Funcția care se execută automat când se declanșează întreruperea INT0 (pin 2)
ISR(INT0_vect) {
  // Magnetul validează deschiderea DOAR dacă sistemul a trecut deja de pasul cu PIN-ul
  if (stareCurenta == ASTEPTARE_MAGNET) {
    magnetDetectat = true; // Setăm semnalizatorul (flag) că senzorul a citit magnetul
  }
}

void setup() {
  // --- Initializare EEPROM ---
  // Dacă EEPROM-ul este gol (255 e valoarea default la un chip nou), scriem un PIN inițial "1234"
  if (EEPROM.read(0) == 255) {
    EEPROM.update(0, '1');
    EEPROM.update(1, '2');
    EEPROM.update(2, '3');
    EEPROM.update(3, '4');
  }
  
  // Citim cele 4 caractere din EEPROM și le concatenăm în variabila pinSalvat
  for (int i = 0; i < 4; i++) {
    pinSalvat += (char)EEPROM.read(i);
  }

  // --- Initializare Periferice ---
  lcd.init();       // Inițializarea ecranului LCD
  lcd.backlight();  // Aprinderea luminii de fundal a ecranului
  
  usaSeif.attach(pinServo); // Conectăm servomotorul la pinul 11
  usaSeif.write(90);        // Setăm servo-ul la 90 de grade (punctul de STOP pentru servo-urile cu rotație continuă 360)
  
  pinMode(pinBuzzer, OUTPUT); // Configurăm pinul buzzer-ului ca ieșire
  pinMode(pinLaser, OUTPUT);  // Configurăm pinul laserului ca ieșire
  digitalWrite(pinLaser, LOW); // Ne asigurăm că laserul este stins la pornire

  pinMode(pinR, OUTPUT); // Configurare pin componentă Roșie LED
  pinMode(pinG, OUTPUT); // Configurare pin componentă Verde LED
  pinMode(pinB, OUTPUT); // Configurare pin componentă Albastră LED
  
  pinMode(pinSenzor, INPUT_PULLUP); // Senzorul pe pin 2 cu rezistență internă de pull-up activată

  // --- CONFIGURARE REGISTRE PENTRU INT0 (Hardware Interrupts) ---
  cli(); // Dezactivăm global întreruperile cât timp configurăm regiștrii
  EICRA |= (1 << ISC01);   // Setăm bitul ISC01 din registrul de control (EICRA)
  EICRA &= ~(1 << ISC00);  // Ștergem bitul ISC00 -> Configurația înseamnă declanșare pe "Falling edge" (trecere din HIGH în LOW)
  EIMSK |= (1 << INT0);    // Activăm masca de întrerupere pentru INT0 (pinul 2)
  sei(); // Reactivăm global întreruperile
  // ----------------------------------------

  afiseazaEcranBlocat(); // Afișăm ecranul principal de așteptare
}

void loop() {
  char tasta = tastatura.getKey(); // Citim apăsarea curentă de pe tastatură

  // Verificăm în ce stare se află sistemul pentru a ști cum interpretăm apăsările
  switch (stareCurenta) {
    
    case BLOCAT: // Așteptăm introducerea PIN-ului
      if (tasta) {
        if (tasta == '*') { // Tasta '*' funcționează ca un buton de ștergere (Resetare input)
          pinIntrodus = "";
          afiseazaEcranBlocat();
        } 
        else if (tasta == '#') { // Tasta '#' este folosită ca "Enter" pentru a confirma PIN-ul introdus
          verificaPIN();
        } 
        else if (tasta == 'A') { // Tasta 'A' declanșează procesul de schimbare a PIN-ului
          stareCurenta = SCHIMBARE_PIN_VECHI; // Trecem în starea de schimbare a PIN-ului
          pinIntrodus = "";                   // Curățăm buffer-ul
          seteazaRGB(LOW, LOW, HIGH);         // Setăm LED-ul pe Albastru
          lcd.clear();
          lcd.print("PIN-ul Vechi:");
          lcd.setCursor(0, 1);                // Mutăm cursorul pe a doua linie a LCD-ului
        }
        else {
          // Prevenim overflow-ul dacă cineva apasă prea multe taste (limită 16 caractere)
          if (pinIntrodus.length() < 16) {
            pinIntrodus += tasta; // Adăugăm tasta la șirul PIN-ului
            lcd.print('*');       // Afișăm o steluță pentru a masca PIN-ul real
            emiteSunet(50);       // Bip scurt la fiecare apăsare de tastă
          }
        }
      }
      break;

    case SCHIMBARE_PIN_VECHI: // Solicităm PIN-ul vechi pentru a aproba modificarea
      if (tasta) {
        if (tasta == '*') { // Anulare acțiune
          stareCurenta = BLOCAT;
          pinIntrodus = "";
          afiseazaEcranBlocat();
        } 
        else if (tasta == '#') { // Validăm dacă PIN-ul vechi e corect
          if (pinIntrodus == pinSalvat) {
            stareCurenta = SCHIMBARE_PIN_NOU; // Trecem la pasul următor: setarea noului PIN
            pinIntrodus = "";
            lcd.clear();
            lcd.print("PIN Nou (4 cif):");
            lcd.setCursor(0, 1);
            emiteSunet(200); // Bip lung de confirmare a parolei vechi
          } else {
            // Dacă s-a greșit PIN-ul vechi, abandonăm procesul
            lcd.clear();
            lcd.print("PIN Incorect!");
            emiteSunet(500); delay(2000); // Bip de eroare și pauză de 2 secunde
            stareCurenta = BLOCAT;
            pinIntrodus = "";
            afiseazaEcranBlocat();
          }
        } 
        else {
          // Tastare normală de cifre, mascate cu steluțe
          if (pinIntrodus.length() < 16) {
            pinIntrodus += tasta;
            lcd.print('*');
            emiteSunet(50);
          }
        }
      }
      break;

    case SCHIMBARE_PIN_NOU: // Salvăm un PIN nou în EEPROM
      if (tasta) {
        if (tasta == '*') { // Anulare acțiune
          stareCurenta = BLOCAT;
          pinIntrodus = "";
          afiseazaEcranBlocat();
        } 
        else if (tasta == '#') { // Confirmare noul PIN
          if (pinIntrodus.length() == 4) { // Ne asigurăm că PIN-ul are exact 4 caractere
            // Scriem noile 4 caractere peste primele 4 adrese din memoria EEPROM
            for (int i = 0; i < 4; i++) {
              EEPROM.update(i, pinIntrodus[i]); // .update() scrie doar dacă valoarea e diferită (salvează cicluri de scriere)
            }
            pinSalvat = pinIntrodus; // Actualizăm și variabila din memoria RAM
            lcd.clear();
            lcd.print("PIN Salvat!");
            emiteSunet(100); delay(50); emiteSunet(100); delay(1500); // Sunet de succes (dublu bip)
            
            stareCurenta = BLOCAT; // Revenim la ecranul de start
            pinIntrodus = "";
            afiseazaEcranBlocat();
          } else {
            // Dacă PIN-ul nou are mai mult sau mai puțin de 4 cifre
            lcd.clear();
            lcd.print("Eroare! Doar 4.");
            emiteSunet(500); delay(2000);
            lcd.clear();
            lcd.print("PIN Nou (4 cif):");
            lcd.setCursor(0, 1);
            pinIntrodus = ""; // Golim buffer-ul pentru a încerca din nou
          }
        } 
        else {
          // Aici PIN-ul nou este vizibil în timp ce se tastează, cu limită de 4 caractere
          if (pinIntrodus.length() < 4) {
            pinIntrodus += tasta;
            lcd.print(tasta); // Afișăm tasta efectiv pe ecran
            emiteSunet(50);
          }
        }
      }
      break;

    case ASTEPTARE_MAGNET: // PIN introdus corect, așteptăm factorul 2 (Senzorul Hall)
      if (magnetDetectat) { // Variabila devine 'true' în funcția ISR când magnetul este apropiat de senzor
        magnetDetectat = false; // Resetăm flag-ul pentru utilizări viitoare
        stareCurenta = DEBLOCAT; // Trecem în starea finală de succes
        deschideSeif();         // Apelăm funcția care învârte servomotorul
      }
      if (tasta == '*') { // Dacă utilizatorul se răzgândește sau renunță, tasta '*' anulează pasul 2
        stareCurenta = BLOCAT;
        pinIntrodus = "";
        afiseazaEcranBlocat();
      }
      break;

    case DEBLOCAT: // Starea în care ușa este menținută deschisă
      delay(5000); // Seiful stă deschis timp de 5 secunde
      
      usaSeif.write(0);   // Servomotorul se mișcă în direcție inversă pentru a încuia
      delay(500);         // Se lasă servo-ul să se rotească 500ms
      usaSeif.write(90);  // Oprire servomotor
      
      emiteSunet(100); emiteSunet(100); // Sunet de finalizare/închidere
      
      stareCurenta = BLOCAT; // Resetăm la starea inițială
      pinIntrodus = "";
      afiseazaEcranBlocat();
      break;

    case MOD_APARARE: // S-au introdus 3 PIN-uri greșite
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ALARM INTRUS!");
      lcd.setCursor(0, 1);
      lcd.print("SISTEM BLOCAT");
      
      digitalWrite(pinLaser, HIGH); // Aprindem modulul Laser

      // Buclă pentru un efect de Sirena vizuală și auditivă de aprox 4 secunde
      for (int i = 0; i < 10; i++) {
        seteazaRGB(HIGH, LOW, LOW); // LED-ul luminează Roșu aprins
        tone(pinBuzzer, 1000);      // Sunet frecvență înaltă
        delay(200);
        seteazaRGB(LOW, LOW, LOW);  // Stingem LED-ul
        tone(pinBuzzer, 500);       // Sunet frecvență joasă (efect de tip "weeo-weeo")
        delay(200);
      }
      
      noTone(pinBuzzer); // Oprim sunetul complet
      digitalWrite(pinLaser, LOW); // Stingem laserul
      
      incercariGresite = 0; // Resetăm contorul de erori pentru a oferi din nou o șansă
      stareCurenta = BLOCAT; // Întoarcere la așteptarea PIN-ului
      pinIntrodus = "";
      afiseazaEcranBlocat();
      break;
  }
}

// --- Functii Auxiliare ---

// Funcție pentru controlul simplificat al LED-ului RGB
void seteazaRGB(bool r, bool g, bool b) {
  digitalWrite(pinR, r); // Aprinde/stinge componenta Roșie
  digitalWrite(pinG, g); // Aprinde/stinge componenta Verde
  digitalWrite(pinB, b); // Aprinde/stinge componenta Albastră
}

// Funcție pentru afișarea interfeței standard de bază (LED Roșu și mesaj LCD)
void afiseazaEcranBlocat() {
  seteazaRGB(HIGH, LOW, LOW); // Starea de repaus, semnalizată prin LED Roșu
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Introduceti PIN:");
  lcd.setCursor(0, 1);        // Poziționează cursorul pe linia a 2-a pentru introducerea caracterelor
}

// Funcție care verifică parola și contorizează greșelile
void verificaPIN() {
  lcd.clear();
  lcd.setCursor(0, 0);
  
  if (pinIntrodus == pinSalvat) { // Verificare condiție de succes
    incercariGresite = 0;         // Resetăm contorul la un PIN corect
    stareCurenta = ASTEPTARE_MAGNET; // Avansăm mașina de stări la Pasul 2
    magnetDetectat = false;       // Curățăm preventiv flag-ul senzorului
    seteazaRGB(LOW, LOW, HIGH);   // LED Albastru pentru stadiul intermediar (2FA)
    lcd.print("PIN OK. Pas 2:");
    lcd.setCursor(0, 1);
    lcd.print("Scanati Magnet!");
    emiteSunet(200); 
  } else {
    incercariGresite++; // Incrementăm greșelile
    
    // Verificăm dacă s-a atins limita de siguranță
    if (incercariGresite >= 3) {
      stareCurenta = MOD_APARARE; // Sistemul intră în alarma antiefracție
      pinIntrodus = "";
    } else {
      // Afișează numărul de încercări rămase înainte de alarmă
      lcd.print("PIN Incorect!");
      lcd.setCursor(0, 1);
      lcd.print("Incercari: ");
      lcd.print(3 - incercariGresite); 
      emiteSunet(500); 
      delay(2000); // Ține ecranul cu eroare vizibil 2 secunde
      pinIntrodus = "";
      afiseazaEcranBlocat();
    }
  }
}

// Funcția de declanșare a motorului pentru deschiderea efectivă a seifului
void deschideSeif() {
  seteazaRGB(LOW, HIGH, LOW); // LED Verde - Succes total
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ACCES APROBAT");
  lcd.setCursor(0, 1);
  lcd.print("Seiful e deschis");
  
  // Arpegiu scurt de aprobare a accesului
  emiteSunet(100); delay(50); emiteSunet(150); delay(50); emiteSunet(300);
  
  usaSeif.write(180); // Învârte servomotorul pentru a debloca yala (pe directia 'înainte')
  delay(500);         // Se învârte 500 de milisecunde 
  usaSeif.write(90);  // Oprește servomotorul (la cele cu rotație continuă, 90 înseamnă repaus)
}

// Funcție utilitară pentru a acționa buzzer-ul pentru o anumită durată
void emiteSunet(int durata) {
  digitalWrite(pinBuzzer, HIGH); // Pornește difuzorul
  delay(durata);                 // Așteaptă X milisecunde
  digitalWrite(pinBuzzer, LOW);  // Oprește difuzorul
}
