# 🔒 Sistem Embedded de Securitate Multi-Factor (2FA) cu Răspuns Activ

Acest repository conține codul sursă și documentația tehnică pentru un prototip funcțional al unei console de securitate pentru seifuri de înaltă siguranță. Sistemul este construit pe arhitectura AVR (ATmega328P) și utilizează o mașină cu stări finite (FSM) pentru a garanta o execuție fluidă, non-blocantă și un control predictibil al senzorilor critici.


## 🌟 Caracteristici Principale și Arhitectură
* **Autentificare Multi-Factor (2FA):** Accesul este condiționat de doi factori succesivi: un cod PIN din 4 cifre și o cheie fizică (magnetică).
* **Control prin FSM (Finite State Machine):** Logica software nu folosește bucle de așteptare (delay-uri blocante), ci împarte execuția în stări mutual exclusive (BLOCAT, ASTEPTARE_MAGNET, DEBLOCAT, MOD_APARARE). 
* **Întreruperi Hardware (Hardware Interrupts):** Pentru a demonstra stăpânirea arhitecturii low-level, senzorul magnetic nu este citit prin "polling", ci declanșează o întrerupere hardware externă pe pinul `INT0` (prin manipularea directă a regiștrilor `EICRA` și `EIMSK` pe *falling edge*).
* **Persistența Datelor (EEPROM):** Parola master este salvată în memoria nevolatilă internă a microcontrolerului. Funcția de update scrie datele optimizat, prelungind durata de viață a celulelor de memorie.
* **Apărare Activă Anti-Intruziune:** Sistemul implementează un mecanism de tip *failsafe*. La a treia introducere eronată a PIN-ului, sistemul blochează tastatura, aprinde un actuator vizual (Laser) și declanșează o sirenă de alarmă alternantă.

## 🛠️ Componente Hardware
* **Unitate Centrală:** Clonă Arduino Uno (CH340) cu microcontroler ATmega328P.
* **Interfață de Intrare:** Tastatură Matricială 4x4 (scanată secvențial) și Senzor Magnetic cu Efect Hall.
* **Feedback și Afișaj:** LCD 1602 controlat prin interfață I2C (PCF8574) pentru reducerea numărului de pini utilizați, Modul LED RGB (Catod Comun) și Buzzer Activ.
* **Actuatoare:** Servomotor SG90 (360°) controlat prin semnal PWM pentru retragerea zăvorului mecanic și Modul Diodă Laser KY-008 pentru modul defensiv.

## ⚙️ Modul de Funcționare
1. **Modul Blocat:** La alimentare, se citește PIN-ul din EEPROM, actuatorul mecanic este forțat în poziția de repaus, iar LED-ul emite lumină roșie.
2. **Validarea Primară:** Utilizatorul tastează codul. Caracterele sunt ascunse pe LCD sub formă de asteriscuri `*`. Dacă PIN-ul coincide, sistemul trece la stadiul următor.
3. **Validarea Secundară:** LED-ul comută pe albastru. Sistemul așteaptă validarea hardware; apropierea magnetului modifică un *flag volatile* direct din rutina de tratare a întreruperii (ISR).
4. **Acces Permis:** LED-ul devine verde, iar servomotorul retrage zăvorul timp de 5 secunde. După expirarea timpului, seiful se reînarmează automat.
5. **Meniu de Administrare Autonom:** Apăsarea tastei `A` permite reconfigurarea parolei fără a reconecta sistemul la PC, solicitând validarea parolei vechi înainte de a scrie noul PIN.

## 📂 Documentație Completă
Pentru detalii aprofundate despre configurația exactă a pinilor, maparea interfeței I2C și fundamentarea teoretică a mașinii de stări, vă invit să consultați fișierul `documentatieproiect.pdf` inclus în acest repository.
