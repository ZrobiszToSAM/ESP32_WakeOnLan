/*
 * Projekt: ESP32 Telegram Wake-On-LAN
 * Wersja: 2.9 (Ukryte /forceupdate + Auto-odświeżanie menu + USB Debugger)
 * Autor: Zrobisz To SAM 2026
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiManager.h> 
#include <Preferences.h>
#include <time.h> 
#include <ESPping.h> 
#include <HTTPClient.h>
#include <HTTPUpdate.h>

#define CURRENT_FIRMWARE_VERSION 2.9 

// --- AUTOMATYCZNY DOBÓR PINU LED ---
#if CONFIG_IDF_TARGET_ESP32S3
  #define LED_PIN 48       
#else
  #define LED_PIN 2        
#endif

#define NUMPIXELS 1      
#define BOOT_BUTTON_PIN 0 

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// --- ZMIENNE GLOBALNE ---
char bot_token[70] = "";
char chat_id_1[20] = "";
char chat_id_2[20] = "";
char mac_address_str[20] = "";
char restart_hour_str[3] = "02"; 
char restart_minute_str[3] = "00";
char portal_timeout_str[5] = "3";   
char wol_target_str[10] = "255";    
char pc_ip_str[16] = "";            
char silent_restart_str[5] = "On";  
char boot_action_str[10] = "WAKE";  

char ota_hour_str[3] = "03";
char ota_minute_str[3] = "00";
char auto_ota_str[10] = "INFO"; 

bool shouldSaveConfig = false;
bool wifiLost = false; 
unsigned long lastWiFiCheck = 0;

int bootButtonState;             
int lastBootButtonState = HIGH;   
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

bool pingActive = false;
unsigned long pingStartTime = 0;
unsigned long lastPingTime = 0;
String requestingChatId = ""; 

bool updateScheduled = false;
String updateChatId = "";
bool autoOtaCheckedThisMonth = false; 

// ==========================================
// MASZYNA STANÓW
// ==========================================
enum BotState {
  STATE_IDLE,
  STATE_WAITING_IP,
  STATE_WAITING_RESTART_TIME,
  STATE_WAITING_OTA_TIME,
  STATE_WAITING_PORTAL_TIMEOUT
};
BotState userState = STATE_IDLE; 

// ==========================================
// KLAWIATURY (GŁÓWNA, USTAWIENIA, AKTUALIZACJE)
// ==========================================
String mainKeyboard = "[[\"⚡ WAKE\"],[\"📊 /info\", \"🔍 /ping\"],[\"🛠️ /settings\"]]";

String inlineSettings = "["
  "[{\"text\":\"🌐 Zmień IP\",\"callback_data\":\"btn_setip\"}, {\"text\":\"⏰ Godzina restartu\",\"callback_data\":\"btn_settime\"}],"
  "[{\"text\":\"🔇 Cichy restart\",\"callback_data\":\"btn_setsil\"}, {\"text\":\"⏱️ Czas portalu\",\"callback_data\":\"btn_setout\"}],"
  "[{\"text\":\"📡 Tryb Wake On LAN\",\"callback_data\":\"btn_setwol\"}],"
  "[{\"text\":\"🔄 Ustawienia aktualizacji\",\"callback_data\":\"btn_updates_menu\"}],"
  "[{\"text\":\"⚙️ Uruchom Portal Wi-Fi\",\"callback_data\":\"btn_config\"}]"
"]";

String inlineUpdateMenu = "["
  "[{\"text\":\"🌙 Godzina Update\",\"callback_data\":\"btn_setota\"}, {\"text\":\"🔄 Auto Update\",\"callback_data\":\"btn_setauto\"}],"
  "[{\"text\":\"🚀 Sprawdź aktualizację\",\"callback_data\":\"btn_checkota\"}],"
  "[{\"text\":\"⬅️ Powrót do ustawień\",\"callback_data\":\"btn_back_settings\"}]"
"]";

Preferences preferences;
WiFiClientSecure secured_client;
UniversalTelegramBot *bot;

int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

WiFiManagerParameter* p_custom_bot_token;
WiFiManagerParameter* p_custom_chat_id_1;
WiFiManagerParameter* p_custom_chat_id_2;
WiFiManagerParameter* p_custom_mac;
WiFiManagerParameter* p_custom_ip;
WiFiManagerParameter* p_custom_rst_hour;
WiFiManagerParameter* p_custom_rst_min;
WiFiManagerParameter* p_custom_tout;
WiFiManagerParameter* p_custom_tgt;
WiFiManagerParameter* p_custom_sil;
WiFiManagerParameter* p_custom_boot;
WiFiManagerParameter* p_custom_auto_ota;

// --- FUNKCJA DIAGNOSTYCZNA: POWÓD RESTARTU PROCESORA ---
String getResetReasonStr() {
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
    case ESP_RST_POWERON:   return "Włączenie zasilania (Power-On) [OK]";
    case ESP_RST_SW:        return "Restart programowy (ESP.restart) [OK]";
    case ESP_RST_PANIC:     return "BŁĄD KRYTYCZNY KODU (Exception / Panic)!";
    case ESP_RST_INT_WDT:   return "BŁĄD: Zawieszenie przerwania (Interrupt Watchdog)!";
    case ESP_RST_TASK_WDT:  return "BŁĄD: Zawieszenie pętli (Task Watchdog)!";
    case ESP_RST_WDT:       return "BŁĄD: Pozostały Watchdog!";
    case ESP_RST_BROWNOUT:  return "BŁĄD ZASILANIA: Spadek napięcia (Brownout)!";
    default:                return "Inny powód (Kod: " + String((int)reason) + ")";
  }
}

void saveConfigCallback () {
  Serial.println("[PORTAL] Zapisywanie nowych danych z portalu konfiguracyjnego...");
  preferences.begin("wolConfig", false);
  preferences.putString("bot_token", String(p_custom_bot_token->getValue()));
  preferences.putString("chat_id_1", String(p_custom_chat_id_1->getValue()));
  preferences.putString("chat_id_2", String(p_custom_chat_id_2->getValue()));
  preferences.putString("mac_address", String(p_custom_mac->getValue()));
  preferences.putString("pc_ip", String(p_custom_ip->getValue()));
  preferences.putString("wol_tgt", String(p_custom_tgt->getValue()));
  preferences.putString("rst_hour", String(p_custom_rst_hour->getValue()));
  preferences.putString("rst_min", String(p_custom_rst_min->getValue()));
  preferences.putString("boot_act", String(p_custom_boot->getValue()));
  preferences.putString("auto_ota", String(p_custom_auto_ota->getValue()));
  
  String tOutStr = String(p_custom_tout->getValue());
  int tOutVal = tOutStr.toInt();
  if (tOutVal > 0 && tOutVal <= 60) preferences.putString("portal_tout", tOutStr);
  else preferences.putString("portal_tout", "3");

  String silStr = String(p_custom_sil->getValue());
  if (silStr.equalsIgnoreCase("On") || silStr.equalsIgnoreCase("Off")) preferences.putString("silent_rst", silStr);
  else preferences.putString("silent_rst", "On");

  preferences.end();
  shouldSaveConfig = true;
  Serial.println("[PORTAL] Dane pomyślnie zapisano w pamięci NVS! [OK]");
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("[WIFI] OSTRZEŻENIE: Uruchomiono Access Point (Tryb Konfiguracji)!");
  Serial.println("[WIFI] Połącz się z siecią: WakeOnLan_Konfiguracja -> http://192.168.4.1");
  pixels.setPixelColor(0, pixels.Color(255, 165, 0)); pixels.show();
}

void ledSetColor(int r, int g, int b) { pixels.setPixelColor(0, pixels.Color(r, g, b)); pixels.show(); }
void ledFadeInGreen() {
  pixels.clear();
  for(int i = 0; i <= 255; i += 5) { ledSetColor(0, i, 0); delay(20); }
  delay(1000); pixels.clear(); pixels.show(); 
}
void ledBlinkAngryRed() {
  for(int i = 0; i < 10; i++) { ledSetColor(255, 0, 0); delay(100); pixels.clear(); pixels.show(); delay(100); }
}

void factoryReset() {
  Serial.println("[SYSTEM] OSTRZEŻENIE: Rozpoczynam twardy reset do ustawień fabrycznych!");
  ledSetColor(255, 0, 0); WiFiManager wm; wm.resetSettings();
  preferences.begin("wolConfig", false); preferences.clear(); preferences.end();
  Serial.println("[SYSTEM] Pamięć NVS oraz Wi-Fi wyczyszczona. Restartuję... [OK]");
  delay(2000); ESP.restart(); 
}

void sendWakeOnLan() {
  byte macAddress[6]; int values[6];
  if(6 == sscanf(mac_address_str, "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5])) {
    for(int i = 0; i < 6; ++i) macAddress[i] = (byte)values[i];
  } else {
    Serial.println("[WOL] BŁĄD: Nieprawidłowy format adresu MAC (" + String(mac_address_str) + ")! Pakiet nie został wysłany.");
    return;
  }
  WiFiUDP udp; byte preamble[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; byte magicPacket[102];
  for(int i = 0; i < 6; i++) magicPacket[i] = preamble[i];
  for(int i = 6; i < 102; i++) magicPacket[i] = macAddress[i % 6];
  IPAddress broadcastIp;
  if (String(wol_target_str) == "BCAST") broadcastIp = WiFi.broadcastIP();
  else broadcastIp = IPAddress(255, 255, 255, 255);
  udp.beginPacket(broadcastIp, 9); udp.write(magicPacket, 102); udp.endPacket();
  
  Serial.println("[WOL] Wysłano Magic Packet (102 bajty) -> MAC: " + String(mac_address_str) + " | Cel IP: " + broadcastIp.toString() + " [OK]");
}

bool isValidIP(String ip) {
  if (ip.equalsIgnoreCase("none")) return true;
  int dots = 0;
  for (int i = 0; i < ip.length(); i++) {
    if (ip.charAt(i) == '.') dots++;
    else if (!isDigit(ip.charAt(i))) return false; 
  }
  if (dots != 3) return false;
  int parts[4]; int partIndex = 0; int start = 0;
  for (int i = 0; i <= ip.length(); i++) {
    if (i == ip.length() || ip.charAt(i) == '.') {
      String part = ip.substring(start, i);
      if (part.length() == 0 || part.length() > 3) return false;
      parts[partIndex] = part.toInt();
      if (parts[partIndex] < 0 || parts[partIndex] > 255) return false;
      partIndex++; start = i + 1;
    }
  }
  return true;
}

bool isValidTime(String t) {
  if (t.length() != 5) return false;
  if (t.charAt(2) != ':') return false;
  if (!isDigit(t.charAt(0)) || !isDigit(t.charAt(1)) || !isDigit(t.charAt(3)) || !isDigit(t.charAt(4))) return false;
  int h = t.substring(0, 2).toInt(); int m = t.substring(3, 5).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  return true;
}

String getWiFiBars(int rssi) {
  if (rssi >= -55) return "■■■■ (Doskonały)";
  else if (rssi >= -70) return "■■■□ (Dobry)";
  else if (rssi >= -80) return "■■□□ (Słaby)";
  else return "■□□□ (Bardzo Słaby)";
}

// ==========================================
// WIDOKI MENU USTAWIEŃ I AKTUALIZACJI
// ==========================================
void sendSettingsMenu(String chat_id) {
  Serial.println("[TELEGRAM] Wysyłanie głównego menu /settings z podglądem wartości...");
  String msg = "🛠️ *PANEL STEROWANIA*\n----------------------------\n";
  msg += "🌐 Adres IP PC: " + (String(pc_ip_str).length() > 6 ? String(pc_ip_str) : String("Wyłączony")) + "\n";
  msg += "📡 Tryb WOL: " + String(wol_target_str) + "\n";
  msg += "⏰ Godzina restartu: " + String(restart_hour_str) + ":" + String(restart_minute_str) + "\n";
  msg += "🔇 Cichy restart: " + String(String(silent_restart_str).equalsIgnoreCase("On") ? "Włączony (On)" : "Wyłączony (Off)") + "\n";
  msg += "⏱️ Czas portalu: " + String(portal_timeout_str) + " min\n";
  msg += "----------------------------\nWybierz, co chcesz skonfigurować:";
  
  bot->sendMessageWithInlineKeyboard(chat_id, msg, "Markdown", inlineSettings);
}

void sendUpdateMenu(String chat_id) {
  Serial.println("[TELEGRAM] Wysyłanie podmenu ustawień aktualizacji...");
  String aOta = String(auto_ota_str);
  String autoDesc = "❌ Wyłączony";
  if (aOta == "INFO") autoDesc = "🔔 Powiadomienia";
  else if (aOta == "INSTALL") autoDesc = "🚀 Auto-instalacja";

  String msg = "🔄 *USTAWIENIA AKTUALIZACJI*\n----------------------------\n";
  msg += "📦 Obecna wersja: V" + String(CURRENT_FIRMWARE_VERSION) + "\n";
  msg += "🔄 Auto Update: " + autoDesc + "\n";
  msg += "🌙 Godzina Update: " + String(ota_hour_str) + ":" + String(ota_minute_str) + "\n";
  if (updateScheduled) {
    msg += "⚠️ *Zaplanowano instalację na " + String(ota_hour_str) + ":" + String(ota_minute_str) + "*\n";
  }
  msg += "----------------------------\nWybierz opcję:";

  bot->sendMessageWithInlineKeyboard(chat_id, msg, "Markdown", inlineUpdateMenu);
}

// ==========================================
// POBIERANIE PORADNIKÓW Z GITHUBA (PING & WOL)
// ==========================================
void fetchGitHubHelp(String chat_id, String url) {
  Serial.println("[GITHUB] Pobieranie poradnika z: " + url);
  Serial.println("[RAM] Wolna pamięć przed połączeniem SSL: " + String(ESP.getFreeHeap() / 1024) + " KB");
  bot->sendMessage(chat_id, "⏳ Pobieram poradnik z serwera...", "");
  
  secured_client.stop(); 
  
  WiFiClientSecure client; 
  client.setInsecure();
  HTTPClient http;
  
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("[GITHUB] Pobrano plik (" + String(payload.length()) + " znaków). Wysyłam na Telegram... [OK]");
    bot->sendMessage(chat_id, payload, "");
  } else {
    Serial.println("[GITHUB] BŁĄD pobierania pliku! Kod HTTP: " + String(httpCode));
    bot->sendMessage(chat_id, "❌ Nie udało się pobrać instrukcji z GitHuba. Kod HTTP: " + String(httpCode), "");
  }
  http.end();
  Serial.println("[RAM] Wolna pamięć po zamknięciu sesji GitHub: " + String(ESP.getFreeHeap() / 1024) + " KB");
}

// ==========================================
// OTA
// ==========================================
void executeOTAUpdate(String chat_id) {
  Serial.println("[OTA] Rozpoczynam pobieranie nowego firmware...");
  if (chat_id != "") bot->sendMessage(chat_id, "⬇️ Pobieram pliki. To potrwa kilkanaście sekund...", "");
  
  secured_client.stop(); 
  
  WiFiClientSecure otaClient; otaClient.setInsecure();
  String binUrl = "https://raw.githubusercontent.com/ZrobiszToSAM/ESP32_WakeOnLan/refs/heads/main/Update/bin/firmware_";
  #if CONFIG_IDF_TARGET_ESP32
    binUrl += "esp32.bin";
  #elif CONFIG_IDF_TARGET_ESP32S3
    binUrl += "esp32s3.bin";
  #else
    Serial.println("[OTA] BŁĄD: Nieobsługiwany model procesora!");
    if(chat_id != "") bot->sendMessage(chat_id, "❌ Błąd: Procesor nieobsługiwany.", ""); return;
  #endif
  
  Serial.println("[OTA] Pobieranie z URL: " + binUrl);
  t_httpUpdate_return ret = httpUpdate.update(otaClient, binUrl);
  if (ret == HTTP_UPDATE_FAILED) {
    Serial.println("[OTA] BŁĄD aktualizacji: " + httpUpdate.getLastErrorString());
    if (chat_id != "") bot->sendMessage(chat_id, "❌ Błąd aktualizacji: " + httpUpdate.getLastErrorString(), "");
  } else if (ret == HTTP_UPDATE_NO_UPDATES) {
    Serial.println("[OTA] OSTRZEŻENIE: Brak plików aktualizacji na serwerze.");
    if (chat_id != "") bot->sendMessage(chat_id, "ℹ️ Brak plików.", "");
  }
}

void checkOTAUpdate(String chat_id, bool isAutoCheck = false) {
  Serial.println(String("[OTA] Sprawdzanie dostępności aktualizacji (Tryb: ") + (isAutoCheck ? "AUTO" : "MANUAL") + ")...");
  if (!isAutoCheck) bot->sendMessage(chat_id, "🔄 Łączę z serwerem GitHub i sprawdzam aktualizacje...", "");
  
  secured_client.stop(); 
  
  WiFiClientSecure otaClient; otaClient.setInsecure();
  HTTPClient http;
  String versionUrl = "https://raw.githubusercontent.com/ZrobiszToSAM/ESP32_WakeOnLan/refs/heads/main/Update/bin/version.txt";
  http.begin(otaClient, versionUrl);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString(); payload.trim();
    float remoteVersion = payload.toFloat();
    http.end(); 
    
    Serial.println("[OTA] Wersja lokalna: V" + String(CURRENT_FIRMWARE_VERSION) + " | Wersja na GitHubie: V" + String(remoteVersion) + " [OK]");
    
    if (remoteVersion > CURRENT_FIRMWARE_VERSION) {
      Serial.println("[OTA] Wykryto nowszą wersję oprogramowania!");
      if (isAutoCheck) {
        String autoMode = String(auto_ota_str);
        if (autoMode == "INFO") {
          bot->sendMessage(chat_id, "🔔 *AUTO-POWIADOMIENIE*\nZnaleziono nową wersję na GitHubie (*V" + String(remoteVersion) + "*)!\nUżyj 🛠️ /settings, aby ją zainstalować.", "Markdown");
        } else if (autoMode == "INSTALL") {
          bot->sendMessage(chat_id, "🔔 *AUTO-AKTUALIZACJA*\nRozpoczynam instalację nowej wersji (V" + String(remoteVersion) + ")...", "Markdown");
          executeOTAUpdate(chat_id);
        }
      } else {
        String msg = "🚀 Znaleziono nową wersję (*V" + String(remoteVersion) + "*)!\nCo chcesz zrobić?";
        String inlineOTA = "["
          "[{\"text\":\"✅ Aktualizuj TERAZ\",\"callback_data\":\"ota_now\"}],"
          "[{\"text\":\"🌙 Zaktualizuj w nocy (" + String(ota_hour_str) + ":" + String(ota_minute_str) + ")\",\"callback_data\":\"ota_night\"}],"
          "[{\"text\":\"❌ Anuluj\",\"callback_data\":\"ota_cancel\"}]"
        "]";
        bot->sendMessageWithInlineKeyboard(chat_id, msg, "Markdown", inlineOTA);
      }
    } else {
      Serial.println("[OTA] Posiadasz już najnowszą wersję. [OK]");
      if (!isAutoCheck) bot->sendMessage(chat_id, "✅ Posiadasz już najnowszą wersję (V" + String(CURRENT_FIRMWARE_VERSION) + ").", "");
    }
  } else {
    http.end();
    Serial.println("[OTA] BŁĄD połączenia z GitHubem! Kod HTTP: " + String(httpCode));
    if (!isAutoCheck) bot->sendMessage(chat_id, "❌ Błąd połączenia (HTTP: " + String(httpCode) + ").", "");
  }
}

void clearOldMessages() {
  Serial.println("[TELEGRAM] Czyszczenie zaległych wiadomości z kolejki...");
  int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
  while(numNewMessages) {
    bot->last_message_received = bot->messages[numNewMessages - 1].update_id;
    numNewMessages = bot->getUpdates(bot->last_message_received + 1);
  }
  Serial.println("[TELEGRAM] Kolejka wyczyszczona. Gotowy do pracy! [OK]");
}

// ==========================================
// OBSŁUGA KOMEND Z KONSOLI USB (SERIAL DEBUGGER)
// ==========================================
void handleSerialCommands() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    Serial.println("\n--- [USB KOMENDA: " + cmd + "] ---");
    if (cmd.equalsIgnoreCase("help")) {
      Serial.println("Dostępne komendy lokalne USB:");
      Serial.println("  info   - Wyświetla pełny stan urządzenia i pamięci NVS");
      Serial.println("  wake   - Wysyła pakiet Magic Packet (WOL)");
      Serial.println("  ping   - Pinguje komputer zdefiniowany w ustawieniach");
      Serial.println("  config - Wymusza uruchomienie portalu Wi-Fi (192.168.4.1)");
      Serial.println("  reset  - Przywraca ustawienia fabryczne (czyści NVS i Wi-Fi)");
    }
    else if (cmd.equalsIgnoreCase("info")) {
      Serial.println("Model ESP: " + String(ESP.getChipModel()) + " | Wersja: V" + String(CURRENT_FIRMWARE_VERSION));
      Serial.println("Powód ostatniego startu: " + getResetReasonStr());
      Serial.println("Wi-Fi Status: " + String(WiFi.status() == WL_CONNECTED ? "Połączono [OK]" : "Rozłączono") + " | IP: " + WiFi.localIP().toString() + " | RSSI: " + String(WiFi.RSSI()) + " dBm");
      Serial.println("MAC docelowy: " + String(mac_address_str) + " | Tryb WOL: " + String(wol_target_str));
      Serial.println("Ping IP: " + String(pc_ip_str) + " | Wolny RAM: " + String(ESP.getFreeHeap() / 1024) + " KB");
      Serial.println("Restart o: " + String(restart_hour_str) + ":" + String(restart_minute_str) + " (Cichy: " + String(silent_restart_str) + ")");
      Serial.println("Auto Update: " + String(auto_ota_str) + " | Godzina Update: " + String(ota_hour_str) + ":" + String(ota_minute_str));
    }
    else if (cmd.equalsIgnoreCase("wake")) {
      sendWakeOnLan();
      ledFadeInGreen();
    }
    else if (cmd.equalsIgnoreCase("ping")) {
      IPAddress targetIp;
      if (targetIp.fromString(pc_ip_str)) {
        Serial.println("[PING] Wysyłam testowy ping do: " + String(pc_ip_str) + "...");
        if (Ping.ping(targetIp, 1)) Serial.println("[PING] SUKCES! Komputer odpowiedział! (Włączony) [OK]");
        else Serial.println("[PING] OSTRZEŻENIE: Brak odpowiedzi (Timeout).");
      } else {
        Serial.println("[PING] BŁĄD: Nie ustawiono poprawnego adresu IP!");
      }
    }
    else if (cmd.equalsIgnoreCase("config")) {
      Serial.println("[SYSTEM] Wymuszam start portalu konfiguracyjnego...");
      preferences.begin("wolConfig", false); preferences.putBool("force_config", true); preferences.end();
      delay(1000); ESP.restart();
    }
    else if (cmd.equalsIgnoreCase("reset")) {
      factoryReset();
    }
    else if (cmd.equalsIgnoreCase("forceupdate")) {
      Serial.println("[OTA] TRYB SERWISOWY: Wymuszono pobranie firmware przez USB!");
      executeOTAUpdate("");
    }
    else {
      Serial.println("[USB] Nieznana komenda. Wpisz 'help', aby zobaczyć listę.");
    }
    Serial.println("----------------------------------\n");
  }
}

// ==========================================
// OBSŁUGA WIADOMOŚCI TELEGRAM
// ==========================================
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot->messages[i].chat_id;
    String text = bot->messages[i].text;
    String query_id = bot->messages[i].query_id; 
    String msg_type = bot->messages[i].type;

    Serial.println("[TELEGRAM] Odebrano (" + msg_type + ") od ID: " + chat_id + " -> Treść: '" + text + "'");

    if (chat_id != String(chat_id_1) && (String(chat_id_2).length() == 0 || chat_id != String(chat_id_2))) {
      Serial.println("[TELEGRAM] BŁĄD AUTORYZACJI! Odrzucono dostęp dla Chat ID: " + chat_id);
      bot->sendMessage(chat_id, "Brak autoryzacji!", ""); ledBlinkAngryRed(); continue; 
    }

    if (msg_type == "callback_query") {
      Serial.println("[CALLBACK] Kliknięto przycisk Inline: " + text);
      
      if (text == "btn_updates_menu") {
        userState = STATE_IDLE;
        sendUpdateMenu(chat_id);
      }
      else if (text == "btn_back_settings") {
        userState = STATE_IDLE;
        sendSettingsMenu(chat_id);
      }
      else if (text == "btn_setip") {
        userState = STATE_WAITING_IP;
        Serial.println("[STAN] Zmiana stanu -> STATE_WAITING_IP (Oczekiwanie na wpisanie IP)");
        bot->sendMessage(chat_id, "Wpisz nowy adres IP, który ESP będzie pingować (Wpisz 'none' aby wyłączyć):", "");
      }
      else if (text == "btn_settime") {
        userState = STATE_WAITING_RESTART_TIME;
        Serial.println("[STAN] Zmiana stanu -> STATE_WAITING_RESTART_TIME");
        bot->sendMessage(chat_id, "Wpisz godzinę prewencyjnego restartu (format HH:MM, np. 04:30):", "");
      }
      else if (text == "btn_setota") {
        userState = STATE_WAITING_OTA_TIME;
        Serial.println("[STAN] Zmiana stanu -> STATE_WAITING_OTA_TIME");
        bot->sendMessage(chat_id, "Wpisz preferowaną godzinę nocnej instalacji Update (format HH:MM):", "");
      }
      else if (text == "btn_setauto") {
        String inlineAuto = "["
          "[{\"text\":\"🔔 Powiadamiaj\",\"callback_data\":\"auto_info\"}],"
          "[{\"text\":\"🚀 Instaluj Automatycznie\",\"callback_data\":\"auto_inst\"}],"
          "[{\"text\":\"❌ Wyłącz Auto\",\"callback_data\":\"auto_none\"}]"
        "]";
        bot->sendMessageWithInlineKeyboard(chat_id, "Wybierz tryb automatycznego sprawdzania wersji:", "", inlineAuto);
      }
      else if (text == "btn_setsil") {
        String inlineSil = "[[{\"text\":\"WŁĄCZ (On)\",\"callback_data\":\"sil_on\"}, {\"text\":\"WYŁĄCZ (Off)\",\"callback_data\":\"sil_off\"}]]";
        bot->sendMessageWithInlineKeyboard(chat_id, "Czy nocny prewencyjny restart ma być cichy (bez telegramu)?", "", inlineSil);
      }
      else if (text == "btn_setout") {
        userState = STATE_WAITING_PORTAL_TIMEOUT;
        Serial.println("[STAN] Zmiana stanu -> STATE_WAITING_PORTAL_TIMEOUT");
        bot->sendMessage(chat_id, "Wpisz czas działania portalu Wi-Fi w minutach:", "");
      }
      else if (text == "btn_setwol") {
        String inlineWol = "[[{\"text\":\"255 (Zalecane)\",\"callback_data\":\"wol_255\"}, {\"text\":\"BCAST\",\"callback_data\":\"wol_bcast\"}]]";
        bot->sendMessageWithInlineKeyboard(chat_id, "Wybierz typ broadcastu WOL:", "", inlineWol);
      }
      else if (text == "btn_checkota") {
        checkOTAUpdate(chat_id, false);
      }
      else if (text == "btn_config") {
        Serial.println("[SYSTEM] Użytkownik wywołał Portal Wi-Fi z Telegrama. Restartuję...");
        bot->sendMessage(chat_id, "Uruchamiam tryb konfiguracji (192.168.4.1).", "");
        preferences.begin("wolConfig", false); preferences.putBool("force_config", true); preferences.end();
        delay(1500); ESP.restart();
      }
      else if (text == "btn_fix_ping") {
        fetchGitHubHelp(chat_id, "https://raw.githubusercontent.com/ZrobiszToSAM/ESP32_WakeOnLan/refs/heads/main/Issues/ping_issues.txt");
      }
      else if (text == "btn_fix_wol") {
        fetchGitHubHelp(chat_id, "https://raw.githubusercontent.com/ZrobiszToSAM/ESP32_WakeOnLan/refs/heads/main/Issues/wol_issues.txt");
      }
      
      // --- ZAPIS OPCJI AKTUALIZACJI + AUTO-ODŚWIEŻENIE PODMENU AKTUALIZACJI ---
      else if (text == "auto_info") {
        preferences.begin("wolConfig", false); preferences.putString("auto_ota", "INFO"); preferences.end();
        String("INFO").toCharArray(auto_ota_str, 10);
        Serial.println("[NVS] Zapisano Auto Update: INFO [OK]");
        bot->sendMessage(chat_id, "✅ Auto Update: INFO", "");
        sendUpdateMenu(chat_id);
      }
      else if (text == "auto_inst") {
        preferences.begin("wolConfig", false); preferences.putString("auto_ota", "INSTALL"); preferences.end();
        String("INSTALL").toCharArray(auto_ota_str, 10);
        Serial.println("[NVS] Zapisano Auto Update: INSTALL [OK]");
        bot->sendMessage(chat_id, "✅ Auto Update: INSTALL", "");
        sendUpdateMenu(chat_id);
      }
      else if (text == "auto_none") {
        preferences.begin("wolConfig", false); preferences.putString("auto_ota", "NONE"); preferences.end();
        String("NONE").toCharArray(auto_ota_str, 10);
        Serial.println("[NVS] Zapisano Auto Update: NONE [OK]");
        bot->sendMessage(chat_id, "✅ Auto Update: WYŁĄCZONY.", "");
        sendUpdateMenu(chat_id);
      }
      
      // --- ZAPIS OPCJI GŁÓWNYCH + AUTO-ODŚWIEŻENIE PANELU STEROWANIA ---
      else if (text == "sil_on") {
        preferences.begin("wolConfig", false); preferences.putString("silent_rst", "On"); preferences.end();
        String("On").toCharArray(silent_restart_str, 5);
        Serial.println("[NVS] Zapisano Cichy restart: On [OK]");
        bot->sendMessage(chat_id, "✅ Cichy restart: WŁĄCZONY", "");
        sendSettingsMenu(chat_id);
      }
      else if (text == "sil_off") {
        preferences.begin("wolConfig", false); preferences.putString("silent_rst", "Off"); preferences.end();
        String("Off").toCharArray(silent_restart_str, 5);
        Serial.println("[NVS] Zapisano Cichy restart: Off [OK]");
        bot->sendMessage(chat_id, "✅ Cichy restart: WYŁĄCZONY", "");
        sendSettingsMenu(chat_id);
      }
      else if (text == "wol_255") {
        preferences.begin("wolConfig", false); preferences.putString("wol_tgt", "255"); preferences.end();
        String("255").toCharArray(wol_target_str, 10);
        Serial.println("[NVS] Zapisano Tryb WOL: 255 [OK]");
        bot->sendMessage(chat_id, "✅ Tryb Wake On LAN zmieniony na: 255", "");
        sendSettingsMenu(chat_id);
      }
      else if (text == "wol_bcast") {
        preferences.begin("wolConfig", false); preferences.putString("wol_tgt", "BCAST"); preferences.end();
        String("BCAST").toCharArray(wol_target_str, 10);
        Serial.println("[NVS] Zapisano Tryb WOL: BCAST [OK]");
        bot->sendMessage(chat_id, "✅ Tryb Wake On LAN zmieniony na: BCAST", "");
        sendSettingsMenu(chat_id);
      }
      
      else if (text == "ota_now") { executeOTAUpdate(chat_id); }
      else if (text == "ota_night") {
        updateScheduled = true; updateChatId = chat_id;
        Serial.println("[OTA] Zaplanowano aktualizację na godzinę " + String(ota_hour_str) + ":" + String(ota_minute_str) + " [OK]");
        bot->sendMessage(chat_id, "✅ Przyjęto! Moduł zaktualizuje się o " + String(ota_hour_str) + ":" + String(ota_minute_str), "");
        sendUpdateMenu(chat_id);
      }
      else if (text == "ota_cancel") {
        updateScheduled = false; 
        Serial.println("[OTA] Anulowano proces aktualizacji.");
        bot->sendMessage(chat_id, "❌ Proces instalacji anulowany.", "");
        sendUpdateMenu(chat_id);
      }

      bot->answerCallbackQuery(query_id);
      continue; 
    }

    if (userState != STATE_IDLE && text.startsWith("/")) {
      Serial.println("[STAN] Wykryto komendę systemową. Anuluję oczekiwanie na dane -> STATE_IDLE");
      userState = STATE_IDLE;
    }

    // --- OBSŁUGA WPISYWANIA WARTOŚCI Z KLAWIATURY + AUTO-ODŚWIEŻANIE ---
    if (userState != STATE_IDLE) {
      if (userState == STATE_WAITING_IP) {
        text.trim();
        if (isValidIP(text)) {
          if (text.equalsIgnoreCase("none")) text = "";
          text.toCharArray(pc_ip_str, 16);
          preferences.begin("wolConfig", false); preferences.putString("pc_ip", String(pc_ip_str)); preferences.end();
          Serial.println("[NVS] Zapisano nowy adres IP: '" + text + "' [OK]");
          bot->sendMessage(chat_id, (text == "") ? "✅ Pingowanie wyłączone." : "✅ Adres IP zapisany: " + text, "");
        } else {
          Serial.println("[STAN] BŁĄD walidacji IP: " + text);
          bot->sendMessage(chat_id, "❌ Błędny format IP. Akcja anulowana.", "");
        }
        userState = STATE_IDLE; 
        sendSettingsMenu(chat_id);
      }
      else if (userState == STATE_WAITING_RESTART_TIME) {
        text.trim();
        if (isValidTime(text)) {
          text.substring(0, 2).toCharArray(restart_hour_str, 3); text.substring(3, 5).toCharArray(restart_minute_str, 3);
          preferences.begin("wolConfig", false); preferences.putString("rst_hour", String(restart_hour_str)); preferences.putString("rst_min", String(restart_minute_str)); preferences.end();
          Serial.println("[NVS] Zapisano godzinę restartu: " + text + " [OK]");
          bot->sendMessage(chat_id, "✅ Godzina prewencyjnego restartu zapisana: " + text, "");
        } else {
          Serial.println("[STAN] BŁĄD walidacji czasu restartu: " + text);
          bot->sendMessage(chat_id, "❌ Błędny format (użyj HH:MM). Akcja anulowana.", "");
        }
        userState = STATE_IDLE;
        sendSettingsMenu(chat_id);
      }
      else if (userState == STATE_WAITING_OTA_TIME) {
        text.trim();
        if (isValidTime(text)) {
          text.substring(0, 2).toCharArray(ota_hour_str, 3); text.substring(3, 5).toCharArray(ota_minute_str, 3);
          preferences.begin("wolConfig", false); preferences.putString("ota_hour", String(ota_hour_str)); preferences.putString("ota_min", String(ota_minute_str)); preferences.end();
          Serial.println("[NVS] Zapisano godzinę Update: " + text + " [OK]");
          bot->sendMessage(chat_id, "✅ Godzina Update zapisana: " + text, "");
        } else {
          Serial.println("[STAN] BŁĄD walidacji czasu Update: " + text);
          bot->sendMessage(chat_id, "❌ Błędny format (użyj HH:MM). Akcja anulowana.", "");
        }
        userState = STATE_IDLE;
        sendUpdateMenu(chat_id);
      }
      else if (userState == STATE_WAITING_PORTAL_TIMEOUT) {
        text.trim(); int m = text.toInt();
        if (m > 0 && m <= 60 && text == String(m)) {
          text.toCharArray(portal_timeout_str, 5);
          preferences.begin("wolConfig", false); preferences.putString("portal_tout", String(portal_timeout_str)); preferences.end();
          Serial.println("[NVS] Zapisano czas portalu: " + text + " min [OK]");
          bot->sendMessage(chat_id, "✅ Czas portalu zmieniony na " + text + " min.", "");
        } else {
          Serial.println("[STAN] BŁĄD walidacji czasu portalu: " + text);
          bot->sendMessage(chat_id, "❌ Błąd. Wprowadzono nieprawidłową wartość.", "");
        }
        userState = STATE_IDLE;
        sendSettingsMenu(chat_id);
      }
      Serial.println("[STAN] Powrót do STATE_IDLE");
      continue; 
    }

    // --- KOMENDY GŁÓWNE I UKRYTE ---
    if (text == "/start" || text == "⬅️ Powrót") {
      bot->sendMessageWithReplyKeyboard(chat_id, "Wybierz akcję z menu poniżej:", "", mainKeyboard);
    }
    // UKRYTA KOMENDA SERWISOWA: WYMUSZENIE POBRANIA AKTUALIZACJI
    else if (text.equalsIgnoreCase("/forceupdate") || text.equalsIgnoreCase("/force_update")) {
      Serial.println("[OTA] OSTRZEŻENIE: Wywołano ukrytą komendę /forceupdate! Wymuszam pobranie pliku .bin...");
      bot->sendMessage(chat_id, "⚠️ *TRYB SERWISOWY*\nWymuszam pobranie i instalację oprogramowania z GitHuba (z pominięciem sprawdzania wersji)...", "Markdown");
      executeOTAUpdate(chat_id);
    }
    else if (text == "WAKE" || text == "wake" || text == "⚡ WAKE") {
      if (String(pc_ip_str).length() > 6) {
        bot->sendMessage(chat_id, "Wysyłam pakiet! Oczekuję na uruchomienie systemu...", "");
        pingActive = true; pingStartTime = millis(); lastPingTime = 0; requestingChatId = chat_id;
        Serial.println("[PING] Uruchomiono 120-sekundowe nasłuchiwanie Pingu dla IP: " + String(pc_ip_str));
      } else {
        String inlineWolHelp = "[[{\"text\":\"🆘 Komputer się nie włącza?\",\"callback_data\":\"btn_fix_wol\"}]]";
        bot->sendMessageWithInlineKeyboard(chat_id, "Wysyłam pakiet Magic Packet! ⚡", "", inlineWolHelp);
      }
      ledFadeInGreen(); 
      sendWakeOnLan();  
    }
    else if (text == "/ping" || text == "🔍 /ping") {
      if (String(pc_ip_str).length() > 6) {
        Serial.println("[PING] Ręczne sprawdzenie pingu dla IP: " + String(pc_ip_str));
        bot->sendMessage(chat_id, "📡 Sprawdzam połączenie z komputerem...", "");
        IPAddress targetIp;
        if (targetIp.fromString(pc_ip_str)) {
          if (Ping.ping(targetIp, 1)) {
             Serial.println("[PING] Komputer odpowiedział -> WŁĄCZONY [OK]");
             bot->sendMessage(chat_id, "🟢 Twój PC jest WŁĄCZONY i połączony z siecią!", "");
          } else {
             Serial.println("[PING] OSTRZEŻENIE: Brak odpowiedzi od komputera (WYŁĄCZONY / Timeout).");
             String inlinePingHelp = "[[{\"text\":\"🆘 Dlaczego PC nie odpowiada?\",\"callback_data\":\"btn_fix_ping\"}]]";
             bot->sendMessageWithInlineKeyboard(chat_id, "🔴 Twój PC jest obecnie WYŁĄCZONY (brak odpowiedzi).", "", inlinePingHelp);
          }
        } else {
          Serial.println("[PING] BŁĄD formatu IP w pamięci: " + String(pc_ip_str));
          bot->sendMessage(chat_id, "❌ Błąd formatu IP. Skonfiguruj w /settings.", "");
        }
      } else bot->sendMessage(chat_id, "⚠️ Funkcja Ping niedostępna. Ustaw IP w /settings.", "");
    }
    else if (text == "/info" || text == "📊 /info") {
      Serial.println("[TELEGRAM] Generowanie raportu /info...");
      unsigned long upSeconds = millis() / 1000; int upDays = upSeconds / 86400; int upHours = (upSeconds % 86400) / 3600; int upMins = (upSeconds % 3600) / 60;
      
      String info_msg = "🤖 Raport Systemowy\n----------------------------\n";
      info_msg += "Wersja systemu: V" + String(CURRENT_FIRMWARE_VERSION) + "\n";
      info_msg += "Model ESP: " + String(ESP.getChipModel()) + "\n";
      info_msg += "Zasięg Wi-Fi: " + getWiFiBars(WiFi.RSSI()) + "\n";
      info_msg += "Mój IP: " + WiFi.localIP().toString() + "\n";
      info_msg += "WOL: " + String(wol_target_str) + "\n"; 
      
      String aOta = String(auto_ota_str);
      if (aOta == "INFO") info_msg += "Auto Update: 🔔 Powiadomienia\n";
      else if (aOta == "INSTALL") info_msg += "Auto Update: 🚀 Auto Install\n";
      else info_msg += "Auto Update: ❌ Wyłączony\n";

      if(String(pc_ip_str).length() > 6) {
         info_msg += "Ping IP: " + String(pc_ip_str) + "\n";
         IPAddress tgt; tgt.fromString(pc_ip_str);
         info_msg += "PC Status: " + String(Ping.ping(tgt, 1) ? "🟢 WŁĄCZONY" : "🔴 WYŁĄCZONY") + "\n";
      } else info_msg += "Ping IP: [Brak Danych]\n";
      
      info_msg += "Restart o: " + String(restart_hour_str) + ":" + String(restart_minute_str) + " (Cichy: " + String(silent_restart_str) + ")\n";
      info_msg += "RAM: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
      info_msg += "Uptime: " + String(upDays) + "d " + String(upHours) + "h " + String(upMins) + "m\n";
      if (updateScheduled) info_msg += "\n⚠️ *Zaplanowano Update na " + String(ota_hour_str) + ":" + String(ota_minute_str) + "*\n";
      bot->sendMessage(chat_id, info_msg, "Markdown");
    }
    else if (text == "/settings" || text == "🛠️ /settings") {
      sendSettingsMenu(chat_id);
    }
  }
}

// ======================= SETUP ===========================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==========================================");
  Serial.println("   START SYSTEMU ESP32 WAKE-ON-LAN V" + String(CURRENT_FIRMWARE_VERSION));
  Serial.println("==========================================");
  Serial.println("[BOOT] Model procesora: " + String(ESP.getChipModel()) + " (Pin LED: " + String(LED_PIN) + ")");
  Serial.println("[BOOT] Powód startu: " + getResetReasonStr());
  Serial.println("[BOOT] Wolna pamięć RAM: " + String(ESP.getFreeHeap() / 1024) + " KB");

  WiFi.mode(WIFI_STA); 
  
  pixels.begin(); pixels.setBrightness(50); 
  ledSetColor(0, 0, 255); 
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  Serial.println("[BOOT] Sprawdzanie przycisku BOOT ( przytrzymaj 3 sekundy, aby zresetować )...");
  bool resetTriggered = false;
  for (int i = 0; i < 30; i++) {
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) { resetTriggered = true; break; }
    delay(100);
  }
  if (resetTriggered) factoryReset(); 

  preferences.begin("wolConfig", false);
  String savedToken = preferences.getString("bot_token", ""); savedToken.toCharArray(bot_token, 70); 
  String savedId1 = preferences.getString("chat_id_1", ""); savedId1.toCharArray(chat_id_1, 20);
  String savedId2 = preferences.getString("chat_id_2", ""); savedId2.toCharArray(chat_id_2, 20);
  String savedMac = preferences.getString("mac_address", ""); savedMac.toCharArray(mac_address_str, 20);
  String savedHour = preferences.getString("rst_hour", "02"); savedHour.toCharArray(restart_hour_str, 3);
  String savedMin = preferences.getString("rst_min", "00"); savedMin.toCharArray(restart_minute_str, 3);
  
  String savedTOut = preferences.getString("portal_tout", "3"); 
  if (savedTOut.toInt() <= 0 || savedTOut.toInt() > 60) savedTOut = "3";
  savedTOut.toCharArray(portal_timeout_str, 5);
  
  String savedTgt = preferences.getString("wol_tgt", "255"); savedTgt.toCharArray(wol_target_str, 10);
  String savedIp = preferences.getString("pc_ip", ""); savedIp.toCharArray(pc_ip_str, 16);
  String savedSil = preferences.getString("silent_rst", "On"); savedSil.toCharArray(silent_restart_str, 5);
  String savedBAct = preferences.getString("boot_act", "WAKE"); savedBAct.toCharArray(boot_action_str, 10);
  String savedOtaH = preferences.getString("ota_hour", "03"); savedOtaH.toCharArray(ota_hour_str, 3);
  String savedOtaM = preferences.getString("ota_min", "00"); savedOtaM.toCharArray(ota_minute_str, 3);
  String savedAuto = preferences.getString("auto_ota", "INFO"); savedAuto.toCharArray(auto_ota_str, 10);
  
  bool forceConfig = preferences.getBool("force_config", false);
  if (forceConfig) preferences.putBool("force_config", false); 
  bool silentBoot = preferences.getBool("silent_boot", false);
  if (silentBoot) preferences.putBool("silent_boot", false);
  preferences.end(); 

  Serial.println("[NVS] Wczytano konfigurację z pamięci Flash:");
  Serial.println("  -> Token długość: " + String(strlen(bot_token)) + " znaków | Chat ID 1: " + String(chat_id_1));
  Serial.println("  -> MAC: " + String(mac_address_str) + " | PC IP: " + String(pc_ip_str) + " | WOL: " + String(wol_target_str));
  Serial.println("  -> Czas portalu: " + String(portal_timeout_str) + " min | Restart: " + String(restart_hour_str) + ":" + String(restart_minute_str));

  if (strlen(bot_token) < 10 || strlen(chat_id_1) < 3) {
    Serial.println("[NVS] OSTRZEŻENIE: Brak Tokenu lub Chat ID! Wymuszam otwarcie Portalu Konfiguracyjnego.");
    forceConfig = true;
  }

  p_custom_bot_token = new WiFiManagerParameter("bot_token", "Telegram Bot Token", bot_token, 70);
  p_custom_chat_id_1 = new WiFiManagerParameter("chat_id_1", "Twój Chat ID", chat_id_1, 20);
  p_custom_chat_id_2 = new WiFiManagerParameter("chat_id_2", "Drugi Chat ID (Opcjonalnie)", chat_id_2, 20);
  p_custom_mac = new WiFiManagerParameter("mac_address", "MAC Adres PC", mac_address_str, 20);
  p_custom_ip = new WiFiManagerParameter("pc_ip", "IP Komputera (Puste = Wyłącz)", pc_ip_str, 16);
  p_custom_rst_hour = new WiFiManagerParameter("rst_hour", "Godzina restartu (0-23)", restart_hour_str, 3);
  p_custom_rst_min = new WiFiManagerParameter("rst_min", "Minuta restartu (0-59)", restart_minute_str, 3);
  p_custom_tout = new WiFiManagerParameter("portal_tout", "Czas panelu konfiguracji (min)", portal_timeout_str, 5);
  p_custom_tgt = new WiFiManagerParameter("wol_tgt", "Cel WOL (Wpisz 255 lub BCAST)", wol_target_str, 10);
  p_custom_sil = new WiFiManagerParameter("silent_rst", "Cichy restart (On/Off)", silent_restart_str, 5);
  p_custom_boot = new WiFiManagerParameter("boot_act", "Przycisk BOOT (Wpisz WAKE lub NONE)", boot_action_str, 10);
  p_custom_auto_ota = new WiFiManagerParameter("auto_ota", "Auto Aktualizacja (INFO, INSTALL lub NONE)", auto_ota_str, 10);
  
  String author_html = "<br><div style='text-align: center; color: #aaa; font-size: 14px;'>Wersja " + String(CURRENT_FIRMWARE_VERSION) + " (OTA)<br>Stworzono przez:<br><b>Zrobisz To SAM 2026</b></div>";
  WiFiManagerParameter custom_author("a", author_html.c_str(), "", 0, "");

  WiFiManager wm;
  const char* mac_script = "<script>document.addEventListener('DOMContentLoaded',function(){var m=document.getElementById('mac_address');if(m){m.setAttribute('maxlength','17');m.addEventListener('input',function(e){var v=e.target.value.replace(/[^a-fA-F0-9]/g,'').toUpperCase();var r=v.match(/.{1,2}/g);e.target.value=r?r.join(':').substring(0,17):'';});}});</script>";
  wm.setCustomHeadElement(mac_script);
  
  wm.setSaveConfigCallback(saveConfigCallback); wm.setSaveParamsCallback(saveConfigCallback); 
  wm.setAPCallback(configModeCallback); 
  
  wm.setConfigPortalTimeout(atoi(portal_timeout_str) * 60); 
  wm.setConnectTimeout(15); 
  
  wm.addParameter(p_custom_bot_token); wm.addParameter(p_custom_chat_id_1); wm.addParameter(p_custom_chat_id_2);
  wm.addParameter(p_custom_mac); wm.addParameter(p_custom_ip); wm.addParameter(p_custom_tgt);
  wm.addParameter(p_custom_rst_hour); wm.addParameter(p_custom_rst_min); wm.addParameter(p_custom_sil);
  wm.addParameter(p_custom_tout); wm.addParameter(p_custom_boot); wm.addParameter(p_custom_auto_ota); wm.addParameter(&custom_author);

  Serial.println("[WIFI] Łączenie z siecią Wi-Fi...");
  bool portalStatus = false;
  if (forceConfig) portalStatus = wm.startConfigPortal("WakeOnLan_Konfiguracja"); 
  else portalStatus = wm.autoConnect("WakeOnLan_Konfiguracja"); 

  if (!portalStatus) { 
    Serial.println("[WIFI] BŁĄD: Timeout portalu lub brak połączenia. Restartuję ESP32...");
    delay(1000); ESP.restart(); 
  }
  if (shouldSaveConfig) { 
    Serial.println("[WIFI] Zapisano nowe ustawienia. Restartuję w celu zastosowania...");
    delay(1000); ESP.restart(); 
  }

  Serial.println("[WIFI] Połączono z siecią! Adres IP: " + WiFi.localIP().toString() + " | Zasięg: " + String(WiFi.RSSI()) + " dBm [OK]");
  pixels.clear(); pixels.show(); 
  
  Serial.println("[NTP] Synchronizacja czasu z serwerem pool.ntp.org...");
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
  
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  bot = new UniversalTelegramBot(bot_token, secured_client);

  clearOldMessages();
  if (!silentBoot) {
    Serial.println("[TELEGRAM] Wysyłanie powitania na Chat ID: " + String(chat_id_1));
    bot->sendMessage(chat_id_1, "Agent ESP32 podłączony! ⚡ Wpisz /start.", "");
  } else {
    Serial.println("[SYSTEM] Cichy start aktywny (pominięto powiadomienie na Telegramie).");
  }
  Serial.println("[USB] Debugger aktywny! Wpisz 'help' w konsoli, aby zobaczyć komendy lokalne.\n");
}

// ======================== LOOP ===========================
void loop() {
  // 0. OBSŁUGA KOMEND Z KONSOLI USB
  handleSerialCommands();

  // 1. OBSŁUGA FIZYCZNEGO PRZYCISKU BOOT
  int reading = digitalRead(BOOT_BUTTON_PIN);
  if (reading != lastBootButtonState) lastDebounceTime = millis();
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != bootButtonState) {
      bootButtonState = reading;
      if (bootButtonState == LOW && String(boot_action_str) == "WAKE") { 
          Serial.println("[BUTTON] Wciśnięto fizyczny przycisk BOOT -> Wywołuję WAKE!");
          sendWakeOnLan(); ledFadeInGreen();
          if (String(pc_ip_str).length() > 6) { pingActive = true; pingStartTime = millis(); lastPingTime = 0; requestingChatId = String(chat_id_1); }
      }
    }
  }
  lastBootButtonState = reading;

  // 2. KONTROLA POŁĄCZENIA WI-FI
  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiLost) { 
      Serial.println("[WIFI] OSTRZEŻENIE: Utracono połączenie z routerem! Próba ponownego połączenia...");
      ledSetColor(255, 165, 0); wifiLost = true; lastWiFiCheck = millis(); WiFi.disconnect(); WiFi.reconnect(); 
    } 
    else if (millis() - lastWiFiCheck > 15000) { 
      Serial.println("[WIFI] OSTRZEŻENIE: Nadal brak Wi-Fi. Ponawiam reconnect()...");
      lastWiFiCheck = millis(); WiFi.disconnect(); WiFi.reconnect(); 
    }
    return; 
  } else if (wifiLost) { 
    Serial.println("[WIFI] Połączenie z Wi-Fi zostało odzyskane! IP: " + WiFi.localIP().toString() + " [OK]");
    pixels.clear(); pixels.show(); wifiLost = false; 
  }

  // 3. HARMONOGRAM CZASOWY (OTA & RESTART)
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1000)) {
    
    int ota_h = atoi(ota_hour_str); int ota_m = atoi(ota_minute_str);
    if (timeinfo.tm_hour == ota_h && timeinfo.tm_min == ota_m && updateScheduled) {
      Serial.println("[HARMONOGRAM] Wybiła godzina zaplanowanej aktualizacji OTA!");
      updateScheduled = false; executeOTAUpdate(updateChatId); 
    }

    if (timeinfo.tm_mday == 1 && timeinfo.tm_hour == 4 && timeinfo.tm_min == 0) {
      if (!autoOtaCheckedThisMonth && String(auto_ota_str) != "NONE" && millis() > 300000) {
        Serial.println("[HARMONOGRAM] 1. dzień miesiąca (04:00) -> Automatyczne sprawdzanie aktualizacji...");
        autoOtaCheckedThisMonth = true; 
        checkOTAUpdate(String(chat_id_1), true); 
      }
    }
    if (timeinfo.tm_hour == 5) { autoOtaCheckedThisMonth = false; } 

    int rst_h = atoi(restart_hour_str); int rst_m = atoi(restart_minute_str);
    if (timeinfo.tm_hour == rst_h && timeinfo.tm_min == rst_m && millis() > 300000) {
      Serial.println("[HARMONOGRAM] Wybiła godzina prewencyjnego restartu (" + String(restart_hour_str) + ":" + String(restart_minute_str) + "). Restartuję...");
      if (String(silent_restart_str).equalsIgnoreCase("On")) { preferences.begin("wolConfig", false); preferences.putBool("silent_boot", true); preferences.end(); }
      delay(500);
      ESP.restart();
    }
  }

  // 4. LOGIKA PINGU PO WYSŁANIU PAKIETU WAKE
  if (pingActive && String(pc_ip_str).length() > 6) {
    if (millis() - lastPingTime > 5000) { 
      lastPingTime = millis(); 
      unsigned long elapsedSec = (millis() - pingStartTime) / 1000;
      Serial.println("[PING] Próba pingu do " + String(pc_ip_str) + " (Upłynęło: " + String(elapsedSec) + "s / 120s)...");
      
      IPAddress targetIp;
      if (targetIp.fromString(pc_ip_str)) {
        if (Ping.ping(targetIp, 1)) { 
          Serial.println("[PING] SUKCES! Komputer odpowiedział po " + String(elapsedSec) + " sekundach! [OK]");
          bot->sendMessage(requestingChatId, "✅ Twój PC zarejestrował się w sieci i jest WŁĄCZONY!", ""); 
          pingActive = false; 
        } 
        else if (millis() - pingStartTime > 120000) { 
          Serial.println("[PING] OSTRZEŻENIE: Upłynął limit 120 sekund (Timeout). Komputer nie odpowiedział!");
          String inlineBothHelp = "["
            "[{\"text\":\"🆘 PC się nie włączył? (WOL)\",\"callback_data\":\"btn_fix_wol\"}],"
            "[{\"text\":\"🆘 PC działa, ale brak Pingu?\",\"callback_data\":\"btn_fix_ping\"}]"
          "]";
          bot->sendMessageWithInlineKeyboard(requestingChatId, "⚠️ Zbyt długi czas oczekiwania. PC nie odpowiedział w sieci.", "", inlineBothHelp); 
          pingActive = false; 
        }
      } else {
        Serial.println("[PING] BŁĄD konwersji adresu IP! Przerywam nasłuchiwanie.");
        pingActive = false; 
      }
    }
  }

  // 5. ODBIERANIE WIADOMOŚCI Z TELEGRAMA
  if (millis() - lastTimeBotRan > botRequestDelay)  {
    int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
    if (numNewMessages > 0) { handleNewMessages(numNewMessages); bot->last_message_received = bot->messages[numNewMessages - 1].update_id; }
    lastTimeBotRan = millis();
  }
}