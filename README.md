# ⚡ ESP32 Wake-On-LAN Telegram

Projekt poświęcony użytkownikom wszelkiego rodzaju pulpitu zdalnego. Jego największą wadą jest (według mojej opinii) brak możliwości zdalnego uruchamiania komputera. Czasami zdarzała się sytuacja awaryjna, że akurat jakiś plik lub program, który działa tylko na komputerze stacjonarnym, jest potrzebny w "terenie". Z tego faktu powstał ten projekt oparty na bocie na Telegramie. 

Zależało mi na tym, żeby ESP32 nie było tylko "płytką z kabelkami", ale działało jak profesjonalny gadżet – miało ładny interfejs, było odporne na braki prądu i dawało pewność, że komputer faktycznie się włączył.

Kod powstał na potrzeby własne, ale jest również częścią rozwoju kanału **Zrobisz To SAM**.

---

## ✨ Co potrafi ten kod? (Najciekawsze funkcje)

Starałem się upchnąć tu sporo fajnych rozwiązań:

* 📱 **Klikalne menu w Telegramie:** Koniec z wpisywaniem komend z palca. Bot ma zagnieżdżone, wirtualne klawiatury – odpalasz bota i po prostu klikasz przycisk "WAKE" albo "Ustawienia".
* 🔍 **Prawdziwe potwierdzenie (Ping):** Potwierdzenie wybudzenia za pomocą funkcji ping.
* 🛡️ **Blokada przed obcymi:** Bot reaguje tylko na zdefiniowane przez Ciebie Chat ID (możesz dodać 2 osoby). Resztę świata ignoruje, a moduł mruga wtedy ostrzegawczo na czerwono.
* ⚙️ **Konfigurator przez Wi-Fi:** Moduł przy starcie odpala własną sieć do konfiguracji. Wszystkie dane wpisuje się na stronie konfiguratora. Można na niej również wgrać nową wersję programu przez OTA.
* 🔋 **Odporność na zaniki prądu:** Po utracie prądu, co wiąże się również z utratą Wi-Fi, zamiast zawiesić się na zawsze bez Wi-Fi, po 5 minutach moduł sam się zresetuje i spróbuje ponownie.
* 🔘 **Przycisk BOOT:** Przycisk na płytce dostał drugie życie. Dłuższe przytrzymanie na starcie robi Factory Reset, a pojedyncze kliknięcie podczas normalnej pracy działa jak fizyczny przycisk włączania PC.
* 📡 **Zmiana ustawień z czatu:** Zapomniałeś zmienić IP? Zmienisz to komendą w Telegramie, bez wchodzenia w tryb konfiguracji.

---

## 🛠️ Czego potrzebujesz? (Sprzęt)

* Płytka **ESP32** (Projekt optymalizowałem na modelu ESP32-S3 DevKit, ale zadziała na większości).
* Wbudowany przycisk **BOOT** na płytce (pin `0`).

**(Opcjonalnie)**
* Dioda **NeoPixel (WS2812B)** – domyślnie ustawiona na pinie `48`. Robi za nasz wskaźnik statusu.

---

## 📚 Wymagane biblioteki

Zanim klikniesz "Wgraj", zainstaluj te biblioteki w Menedżerze Arduino IDE:

* `UniversalTelegramBot` (autor: Brian Lough)
* `ArduinoJson` (autor: Benoit Blanchon)
* `WiFiManager` (autor: tzapu)
* `Adafruit_NeoPixel` (autor: Adafruit)
* `ESPping` (autorzy: dvarrel, Daniele Colanardi, Marian Craciunescu)

---

## 🤖 Jak stworzyć własnego bota na Telegramie? (Krok po kroku)

Zanim odpalisz moduł, potrzebujesz dwóch rzeczy z Telegrama: **Tokenu Bota** oraz Twojego prywatnego **Chat ID**. Oto jak je zdobyć:

**1. Tworzymy Bota (Token):**
* Otwórz aplikację Telegram i w lupce wyszukaj użytkownika o nazwie `@BotFather` (ma niebieską odznakę weryfikacji).
* Wyślij mu wiadomość `/newbot`.
* Podaj nazwę, która będzie się wyświetlać (np. `Mój Komputer WOL`).
* Podaj unikalny *username* – musi kończyć się słowem "bot" (np. `moj_super_wol_bot`).
* `@BotFather` wyśle Ci wiadomość gratulacyjną. Skopiuj z niej długi ciąg znaków oznaczony jako **HTTP API Token** (np. `123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11`). To wkleisz później w konfiguratorze.

**2. Zdobywamy Twój numer (Chat ID):**
* Wyszukaj w Telegramie bota o nazwie `@userinfobot` (lub `@IDBot`).
* Kliknij na dole przycisk **START**.
* Bot odpisze Ci, podając Twoje `Id` (ciąg samych cyfr, np. `123456789`). To Twój identyfikator, który wkleisz w pole "Twój Chat ID" w konfiguratorze. Dzięki temu ESP32 wpuści tylko Ciebie!

---

## 🚀 Jak to odpalić? (Krok po kroku)

1. Wgraj kod na ESP32.
2. Dioda zaświeci się na niebiesko, a po chwili zmieni kolor na pomarańczowy – to znak, że działa tryb konfiguracji.
3. Znajdź na telefonie/laptopie sieć Wi-Fi o nazwie **`WakeOnLan_Konfiguracja`** i połącz się z nią.
4. Wejdź w przeglądarce na adres `192.168.4.1`.
5. Wpisz swoje dane: Token Bota (z @BotFather), swoje Chat ID (z @userinfobot), Adres MAC komputera, IP do pingu itp.
6. Kliknij **Save**. Płytka zapisze dane w pamięci Flash i uruchomi się ponownie.
7. Wejdź na Telegrama i zobaczysz wiadomość: *"Agent ESP32 podłączony! ⚡ Wpisz /start i wybierz akcję."*
8. Wpisz `/start`, a na dole pojawią się przyciski. Gotowe!

---

## 📱 Jak używać bota?

### 🏠 Menu główne:
* ⚡ **WAKE** - Odpala komputer.
* 📊 **/info** - Wyświetla raport (czas pracy modułu, jakość Wi-Fi, zużycie RAM-u i to, czy PC jest teraz włączony).
* 🔍 **/ping** - Szybkie sprawdzenie z czatu, czy komputer działa.
* 🛠️ **/settings** - Przełącza klawiaturę na drugą stronę (Menu Ustawień).

### 🛠️ Menu ustawień:
* ⚙️ **/config** - Odpala z powrotem pomarańczowy portal konfiguracyjny (nie kasuje Twoich obecnych danych).
* 🔄 **/reset** - **Uwaga!** Twardy reset. Czyści całą pamięć modułu do zera.
* ⬅️ **Powrót** - Powrót do głównych przycisków.

### ⌨️ Komendy ręczne (do wpisania w czacie):
Jeśli chcesz coś szybko zmienić bez wchodzenia w przeglądarkę, możesz wpisać:
* `/setip [adres_ip]` (np. `/setip 192.168.1.50`) - zmienia adres, który bot będzie pingował. Wpisz `none`, żeby wyłączyć pingowanie.
* `/settime [HH:MM]` (np. `/settime 04:30`) - zmienia godzinę nocnego, cichego restartu układu.
* `/setwol [255 lub BCAST]` - zmienia tryb rozgłaszania pakietu WOL. Przydatne, jak masz bardzo oporny router.

---

## 💡 Co mówi dioda (Ściągawka kolorów)

* 🔵 **Niebieski (Start)** - Szukam domowego Wi-Fi.
* 🟠 **Pomarańczowy (Stały)** - Utknąłem/Jestem w trybie konfiguracji. Czekam na wpisanie danych pod 192.168.4.1!
* 🟢 **Zielony (Płynne przejście)** - Wysyłam pakiet Magic Packet.
* 🔴 **Czerwony (Mruganie)** - Ktoś obcy próbował użyć Twojego bota!
* 🔴 **Czerwony (Stały)** - Pamięć Flash jest właśnie czyszczona do zera.