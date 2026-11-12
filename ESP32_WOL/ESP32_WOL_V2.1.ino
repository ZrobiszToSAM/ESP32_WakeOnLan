/*
 * Projekt: ESP32 Telegram Wake-On-LAN
 * Wersja: 2.5
 * Autorzy: Zrobisz To SAM 2026 x Gemini AI
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

// --- KONFIGURACJA PRZYCISKÓW I DIOD ---
#define LED_PIN 48       
#define NUMPIXELS 1      
#define BOOT_BUTTON_PIN 0 

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// --- ZMIENNE GLOBALNE KONFIGURACJI ---
char bot_token[70] = "";
char chat_id_1[20] = "";
char chat_id_2[20] = "";
char mac_address_str[20] = "";
char restart_hour_str[3] = "02"; 
char restart_minute_str[3] = "00";
char portal_timeout_str[5] = "300"; 
char wol_target_str[10] = "255";    
char pc_ip_str[16] = "";            
char silent_restart_str[2] = "1";   
char boot_action_str[10] = "WAKE";  

bool shouldSaveConfig = false;
bool wifiLost = false; 
unsigned long lastWiFiCheck = 0;

// --- ZMIENNE DLA PRZYCISKU BOOT ---
int bootButtonState;             
int lastBootButtonState = HIGH;   
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

// --- ZMIENNE DLA PINGOWANIA ---
bool pingActive = false;
unsigned long pingStartTime = 0;
unsigned long lastPingTime = 0;
String requestingChatId = ""; 

// ==========================================
// SYSTEM MENU: 2 RÓŻNE KLAWIATURY
// ==========================================
String mainKeyboard = "[[\"⚡ WAKE\"],[\"📊 /info\", \"🔍 /ping\"],[\"🛠️ /settings\"]]";
String settingsKeyboard = "[[\"⚙️ /config\", \"🔄 /reset\"],[\"⬅️ Powrót\"]]";

Preferences preferences;
WiFiClientSecure secured_client;
UniversalTelegramBot *bot;

int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

// --- WSKAŹNIKI DO PÓL WIFIMANAGERA ---
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

void saveConfigCallback () {
  Serial.println("Zapisywanie nowych ustawień do pamięci Flash...");
  preferences.begin("wolConfig", false);
  preferences.putString("bot_token", String(p_custom_bot_token->getValue()));
  preferences.putString("chat_id_1", String(p_custom_chat_id_1->getValue()));
  preferences.putString("chat_id_2", String(p_custom_chat_id_2->getValue()));
  preferences.putString("mac_address", String(p_custom_mac->getValue()));
  preferences.putString("pc_ip", String(p_custom_ip->getValue()));
  preferences.putString("wol_tgt", String(p_custom_tgt->getValue()));
  preferences.putString("rst_hour", String(p_custom_rst_hour->getValue()));
  preferences.putString("rst_min", String(p_custom_rst_min->getValue()));
  preferences.putString("portal_tout", String(p_custom_tout->getValue()));
  preferences.putString("silent_rst", String(p_custom_sil->getValue()));
  preferences.putString("boot_act", String(p_custom_boot->getValue()));
  preferences.end();
  shouldSaveConfig = true;
}

void configModeCallback(WiFiManager *myWiFiManager) {
  pixels.setPixelColor(0, pixels.Color(255, 165, 0)); pixels.show();
}

// --- FUNKCJE POMOCNICZE ---
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
  ledSetColor(255, 0, 0); WiFiManager wm; wm.resetSettings();
  preferences.begin("wolConfig", false); preferences.clear(); preferences.end();
  delay(2000); ESP.restart(); 
}

void sendWakeOnLan() {
  byte macAddress[6]; int values[6];
  if(6 == sscanf(mac_address_str, "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5])) {
    for(int i = 0; i < 6; ++i) macAddress[i] = (byte)values[i];
  } else return;

  WiFiUDP udp; byte preamble[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; byte magicPacket[102];
  for(int i = 0; i < 6; i++) magicPacket[i] = preamble[i];
  for(int i = 6; i < 102; i++) magicPacket[i] = macAddress[i % 6];
  
  IPAddress broadcastIp;
  if (String(wol_target_str) == "BCAST") broadcastIp = WiFi.broadcastIP();
  else broadcastIp = IPAddress(255, 255, 255, 255);
  
  udp.beginPacket(broadcastIp, 9); udp.write(magicPacket, 102); udp.endPacket();
}

void clearOldMessages() {
  int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
  while(numNewMessages) {
    bot->last_message_received = bot->messages[numNewMessages - 1].update_id;
    numNewMessages = bot->getUpdates(bot->last_message_received + 1);
  }
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot->messages[i].chat_id;
    String text = bot->messages[i].text;

    if (chat_id != String(chat_id_1) && (String(chat_id_2).length() == 0 || chat_id != String(chat_id_2))) {
      bot->sendMessage(chat_id, "Brak autoryzacji!", ""); ledBlinkAngryRed(); continue; 
    }

    // 1. EKRAN GŁÓWNY (START & POWRÓT)
    if (text == "/start" || text == "⬅️ Powrót") {
      String msg = (text == "⬅️ Powrót") ? "Wróciłeś do menu głównego." : "Wybierz akcję z menu poniżej:";
      bot->sendMessageWithReplyKeyboard(chat_id, msg, "", mainKeyboard);
    }
    // 2. WAKE
    else if (text == "WAKE" || text == "wake" || text == "⚡ WAKE") {
      bot->sendMessage(chat_id, "Wysyłam pakiet! Oczekuję na uruchomienie systemu...", "");
      ledFadeInGreen(); sendWakeOnLan();  
      if (String(pc_ip_str).length() > 6) { pingActive = true; pingStartTime = millis(); lastPingTime = 0; requestingChatId = chat_id; }
    }
    // 3. PING
    else if (text == "/ping" || text == "🔍 /ping") {
      if (String(pc_ip_str).length() > 6) {
        bot->sendMessage(chat_id, "📡 Sprawdzam połączenie z komputerem...", "");
        IPAddress targetIp;
        if (targetIp.fromString(pc_ip_str)) {
          if (Ping.ping(targetIp, 1)) bot->sendMessage(chat_id, "🟢 Twój PC jest WŁĄCZONY i połączony z siecią!", "");
          else bot->sendMessage(chat_id, "🔴 Twój PC jest obecnie WYŁĄCZONY (brak odpowiedzi).", "");
        } else bot->sendMessage(chat_id, "❌ Błąd formatu IP. Ustaw poprawny przez /setip.", "");
      } else bot->sendMessage(chat_id, "⚠️ Funkcja Ping jest niedostępna (brak IP). Wejdź w Ustawienia (/settings).", "");
    }
    // 4. INFO
    else if (text == "/info" || text == "📊 /info") {
      unsigned long upSeconds = millis() / 1000; int upDays = upSeconds / 86400; int upHours = (upSeconds % 86400) / 3600; int upMins = (upSeconds % 3600) / 60;
      String info_msg = "🤖 Raport Systemowy (V2.5)\n----------------------------\n";
      info_msg += "Sieć WiFi: " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)\n";
      info_msg += "Lokalny IP: " + WiFi.localIP().toString() + "\n";
      info_msg += "Tryb WOL: " + String(wol_target_str) + "\n"; // Tutaj znajduje się aktualny tryb wysyłania!
      if(String(pc_ip_str).length() > 6) {
         info_msg += "IP Celu (Ping): " + String(pc_ip_str) + "\n";
         IPAddress tgt; tgt.fromString(pc_ip_str);
         bool ok = Ping.ping(tgt, 1);
         info_msg += "Status PC: " + String(ok ? "🟢 WŁĄCZONY" : "🔴 WYŁĄCZONY") + "\n";
      } else info_msg += "IP Celu (Ping): [Brak Danych]\n";
      info_msg += "Restart zaplanowany na: " + String(restart_hour_str) + ":" + String(restart_minute_str) + "\n";
      info_msg += "RAM: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
      info_msg += "Uptime: " + String(upDays) + "d " + String(upHours) + "h " + String(upMins) + "m\n";
      bot->sendMessageWithReplyKeyboard(chat_id, info_msg, "", mainKeyboard);
    }
    
    // ==========================================
    // 5. EKRAN USTAWIEŃ (SUB-MENU)
    // ==========================================
    else if (text == "/settings" || text == "🛠️ /settings") {
      String set_msg = "🛠️ *MENU USTAWIEŃ*\n\n";
      set_msg += "👉 *Portal Konfiguracyjny (Przycisk pod spodem)*:\n";
      set_msg += "Uruchamia panel przez przeglądarkę, by edytować MAC, Tokeny i sieć Wi-Fi.\n\n";
      set_msg += "👉 *Szybkie zmiany przez czat (Wpisz komendę ręcznie)*:\n";
      set_msg += "🔹 `/setip [adres_ip]` - IP do pingu (np. `/setip 192.168.1.50`). Wpisz `none` aby wyłączyć.\n";
      set_msg += "🔹 `/settime [HH:MM]` - Godzina cichego restartu (np. `/settime 04:30`).\n";
      set_msg += "🔹 `/setwol [tryb]` - Tryb wysyłania pakietu WOL. Wpisz `/setwol 255` lub `/setwol BCAST`.\n";
      bot->sendMessageWithReplyKeyboard(chat_id, set_msg, "Markdown", settingsKeyboard);
    }
    
    // SUB-KOMENDY USTAWIEŃ W CZACIE
    else if (text.startsWith("/setip ")) {
      String new_ip = text.substring(7); new_ip.trim();
      if (new_ip.equalsIgnoreCase("none")) new_ip = ""; 
      new_ip.toCharArray(pc_ip_str, 16);
      preferences.begin("wolConfig", false); preferences.putString("pc_ip", String(pc_ip_str)); preferences.end();
      if (new_ip.length() > 0) bot->sendMessage(chat_id, "✅ Adres IP zapisany: " + new_ip, "");
      else bot->sendMessage(chat_id, "✅ Pingowanie WYŁĄCZONE.", "");
    }
    else if (text.startsWith("/settime ")) {
      String new_time = text.substring(9); new_time.trim();
      if (new_time.length() == 5 && new_time.indexOf(':') == 2) {
        String new_h = new_time.substring(0, 2); String new_m = new_time.substring(3, 5);
        new_h.toCharArray(restart_hour_str, 3); new_m.toCharArray(restart_minute_str, 3);
        preferences.begin("wolConfig", false); preferences.putString("rst_hour", String(restart_hour_str)); preferences.putString("rst_min", String(restart_minute_str)); preferences.end();
        bot->sendMessage(chat_id, "✅ Godzina restartu zapisana: " + new_h + ":" + new_m, "");
      } else bot->sendMessage(chat_id, "❌ Błędny format. Użyj: /settime HH:MM (np. /settime 04:30)", "");
    }
    // NOWA KOMENDA DO WOL
    else if (text.startsWith("/setwol ")) {
      String new_wol = text.substring(8); new_wol.trim();
      new_wol.toUpperCase(); // Zamienia bcast na BCAST
      if (new_wol == "255" || new_wol == "BCAST") {
        new_wol.toCharArray(wol_target_str, 10);
        preferences.begin("wolConfig", false); preferences.putString("wol_tgt", String(wol_target_str)); preferences.end();
        bot->sendMessage(chat_id, "✅ Tryb wysyłania WOL zmieniony na: " + new_wol, "");
      } else {
        bot->sendMessage(chat_id, "❌ Błędna wartość. Wpisz /setwol 255 lub /setwol BCAST", "");
      }
    }
    
    // KOMENDY ODPALANE Z EKRANU USTAWIEŃ
    else if (text == "/config" || text == "⚙️ /config") {
      bot->sendMessage(chat_id, "Uruchamiam tryb konfiguracji (192.168.4.1).", "");
      preferences.begin("wolConfig", false); preferences.putBool("force_config", true); preferences.end();
      delay(1500); ESP.restart(); 
    }
    else if (text == "/reset" || text == "🔄 /reset") {
      bot->sendMessage(chat_id, "Czyszczenie pamięci!", "");
      factoryReset();
    }
  }
}

// ======================= SETUP ===========================
void setup() {
  WiFi.mode(WIFI_OFF); delay(1500); 
  Serial.begin(115200);
  pixels.begin(); pixels.setBrightness(50); 
  ledSetColor(0, 0, 255); 
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

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
  String savedTOut = preferences.getString("portal_tout", "300"); savedTOut.toCharArray(portal_timeout_str, 5);
  String savedTgt = preferences.getString("wol_tgt", "255"); savedTgt.toCharArray(wol_target_str, 10);
  String savedIp = preferences.getString("pc_ip", ""); savedIp.toCharArray(pc_ip_str, 16);
  String savedSil = preferences.getString("silent_rst", "1"); savedSil.toCharArray(silent_restart_str, 2);
  String savedBAct = preferences.getString("boot_act", "WAKE"); savedBAct.toCharArray(boot_action_str, 10);
  
  bool forceConfig = preferences.getBool("force_config", false);
  if (forceConfig) preferences.putBool("force_config", false); 
  
  bool silentBoot = preferences.getBool("silent_boot", false);
  if (silentBoot) preferences.putBool("silent_boot", false);
  preferences.end(); 

  p_custom_bot_token = new WiFiManagerParameter("bot_token", "Telegram Bot Token", bot_token, 70);
  p_custom_chat_id_1 = new WiFiManagerParameter("chat_id_1", "Twój Chat ID", chat_id_1, 20);
  p_custom_chat_id_2 = new WiFiManagerParameter("chat_id_2", "Drugi Chat ID (Opcjonalnie)", chat_id_2, 20);
  p_custom_mac = new WiFiManagerParameter("mac_address", "MAC Adres PC", mac_address_str, 20);
  p_custom_ip = new WiFiManagerParameter("pc_ip", "IP Komputera (Puste = Wyłącz Ping)", pc_ip_str, 16);
  p_custom_rst_hour = new WiFiManagerParameter("rst_hour", "Godzina restartu (0-23)", restart_hour_str, 3);
  p_custom_rst_min = new WiFiManagerParameter("rst_min", "Minuta restartu (0-59)", restart_minute_str, 3);
  p_custom_tout = new WiFiManagerParameter("portal_tout", "Czas Portalu Awarii (sek, np. 300)", portal_timeout_str, 5);
  p_custom_tgt = new WiFiManagerParameter("wol_tgt", "Cel WOL (Wpisz 255 lub BCAST)", wol_target_str, 10);
  p_custom_sil = new WiFiManagerParameter("silent_rst", "Cichy Nocny Restart (1=Tak, 0=Nie)", silent_restart_str, 2);
  p_custom_boot = new WiFiManagerParameter("boot_act", "Przycisk BOOT (Wpisz WAKE lub NONE)", boot_action_str, 10);
  
  WiFiManagerParameter custom_author("a", "<br><div style='text-align: center; color: #aaa; font-size: 14px;'>Wersja 2.5 (Pro Control)<br>Stworzono przez:<br><b>Zrobisz To SAM 2026 x Gemini AI</b></div>", "", 0, "");

  WiFiManager wm;
  const char* mac_script = "<script>document.addEventListener('DOMContentLoaded',function(){var m=document.getElementById('mac_address');if(m){m.setAttribute('maxlength','17');m.addEventListener('input',function(e){var v=e.target.value.replace(/[^a-fA-F0-9]/g,'').toUpperCase();var r=v.match(/.{1,2}/g);e.target.value=r?r.join(':').substring(0,17):'';});}});</script>";
  wm.setCustomHeadElement(mac_script);
  
  wm.setSaveConfigCallback(saveConfigCallback); wm.setSaveParamsCallback(saveConfigCallback); 
  wm.setAPCallback(configModeCallback); 
  
  wm.setConfigPortalTimeout(atoi(portal_timeout_str)); wm.setConnectTimeout(15); 
  
  wm.addParameter(p_custom_bot_token); wm.addParameter(p_custom_chat_id_1); wm.addParameter(p_custom_chat_id_2);
  wm.addParameter(p_custom_mac); wm.addParameter(p_custom_ip); wm.addParameter(p_custom_tgt);
  wm.addParameter(p_custom_rst_hour); wm.addParameter(p_custom_rst_min); wm.addParameter(p_custom_sil);
  wm.addParameter(p_custom_tout); wm.addParameter(p_custom_boot); wm.addParameter(&custom_author);

  bool portalStatus = false;
  if (forceConfig) portalStatus = wm.startConfigPortal("WakeOnLan_Konfiguracja"); 
  else portalStatus = wm.autoConnect("WakeOnLan_Konfiguracja"); 

  if (!portalStatus) { delay(1000); ESP.restart(); }
  if (shouldSaveConfig) { delay(1000); ESP.restart(); }

  pixels.clear(); pixels.show(); 

  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  bot = new UniversalTelegramBot(bot_token, secured_client);

  clearOldMessages();
  
  if (!silentBoot) {
    bot->sendMessage(chat_id_1, "Agent ESP32 podłączony! ⚡ Wpisz /start i wybierz akcję.", "");
  }
}

// ======================== LOOP ===========================
void loop() {
  int reading = digitalRead(BOOT_BUTTON_PIN);
  if (reading != lastBootButtonState) lastDebounceTime = millis();
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != bootButtonState) {
      bootButtonState = reading;
      if (bootButtonState == LOW && String(boot_action_str) == "WAKE") { 
          sendWakeOnLan(); ledFadeInGreen();
          if (String(pc_ip_str).length() > 6) { pingActive = true; pingStartTime = millis(); lastPingTime = 0; requestingChatId = String(chat_id_1); }
      }
    }
  }
  lastBootButtonState = reading;

  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiLost) {
      ledSetColor(255, 165, 0); wifiLost = true; lastWiFiCheck = millis(); WiFi.disconnect(); WiFi.reconnect();
    } else if (millis() - lastWiFiCheck > 15000) { 
      lastWiFiCheck = millis(); WiFi.disconnect(); WiFi.reconnect();
    }
    return; 
  } else if (wifiLost) { pixels.clear(); pixels.show(); wifiLost = false; }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1000)) {
    int rst_h = atoi(restart_hour_str); int rst_m = atoi(restart_minute_str);
    if (timeinfo.tm_hour == rst_h && timeinfo.tm_min == rst_m && millis() > 300000) {
      if (String(silent_restart_str) == "1") { preferences.begin("wolConfig", false); preferences.putBool("silent_boot", true); preferences.end(); }
      ESP.restart();
    }
  }

  if (pingActive && String(pc_ip_str).length() > 6) {
    if (millis() - lastPingTime > 5000) { 
      lastPingTime = millis(); IPAddress targetIp;
      if (targetIp.fromString(pc_ip_str)) {
        if (Ping.ping(targetIp, 1)) { bot->sendMessage(requestingChatId, "✅ Twój PC zarejestrował się w sieci i jest WŁĄCZONY!", ""); pingActive = false; } 
        else if (millis() - pingStartTime > 120000) { bot->sendMessage(requestingChatId, "⚠️ Zbyt długi czas oczekiwania. PC nie odpowiedział na Ping w sieci.", ""); pingActive = false; }
      } else pingActive = false; 
    }
  }

  if (millis() - lastTimeBotRan > botRequestDelay)  {
    int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
    if (numNewMessages > 0) { handleNewMessages(numNewMessages); bot->last_message_received = bot->messages[numNewMessages - 1].update_id; }
    lastTimeBotRan = millis();
  }
}