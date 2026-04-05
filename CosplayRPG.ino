/*
 * ╔══════════════════════════════════════════════════════╗
 * ║            CosplayRPG — by HexLions  v3              ║
 * ║   ESP-01S + SSD1306 0.96" I2C 128x64                 ║
 * ║                                                      ║
 * ║   • Display ruotato 180° (montaggio sottosopra)      ║
 * ║   • Primo avvio: AP captive portal per WiFi          ║
 * ║   • Stats generate dal nome (deterministiche)        ║
 * ║   • Eventi prossimo mese da cosplayersitaliani.it    ║
 * ║   • Carousel eventi: un evento ogni 5s               ║
 * ╚══════════════════════════════════════════════════════╝
 *
 * Librerie richieste (Arduino Library Manager):
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - ArduinoJson  (v6.x)
 *   - NTPClient
 *
 * Connessioni ESP-01S → SSD1306:
 *   GPIO0 → SDA  |  GPIO2 → SCL
 *   3.3V  → VCC  |  GND   → GND
 *
 * ══════════════════════════════════════════════════════
 *  ⚙️  PERSONALIZZA QUI — solo nome e data di nascita
 * ══════════════════════════════════════════════════════
 */

#define CHAR_NAME        "PIETRO"      // Nome (max 8 caratteri, maiuscolo)
#define CHAR_BIRTHDAY    "17/11/1999"

#define AP_SSID        "CosplayRPG-Setup"
#define AP_PASSWORD    "cosplay123"

#define FETCH_INTERVAL 60   // minuti tra un aggiornamento eventi e l'altro

// ══════════════════════════════════════════════════════

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUDP.h>
#include <time.h>

#define SDA_PIN   0
#define SCL_PIN   2
#define OLED_ADDR 0x3C

#define EE_MAGIC_ADDR 0
#define EE_SSID_ADDR  1
#define EE_PASS_ADDR  33
#define EE_MAGIC_VAL  0xAB

// ── Oggetti principali ────────────────────────────────
Adafruit_SSD1306 display(128, 64, &Wire, -1);
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);

// ══════════════════════════════════════════════════════
//  FORWARD DECLARATIONS + DRAW HELPERS
//  (definite qui perché usate prima della loro posizione)
// ══════════════════════════════════════════════════════
void drawFetchProgress(const char* msg);
void drawBoot(const char* msg, int pct);
void drawAPScreen();
void drawConnecting(int tick);

// Linea orizzontale separatore
inline void sep(int y) { display.drawFastHLine(0, y, 128, WHITE); }

// Barra progress generica
inline void drawBar(int x, int y, int w, int h, int val) {
  display.drawRect(x, y, w, h, WHITE);
  int fill = map(constrain(val, 0, 100), 0, 100, 0, w - 2);
  if (fill > 0) display.fillRect(x + 1, y + 1, fill, h - 2, WHITE);
}


// ── Stats personaggio ─────────────────────────────────
int   statStamina, statCraft, statStyle, statHype;
int   charLevel;
String charClass;
int   xpCurrent, xpMax;

// ── Lista eventi (max 20 del prossimo mese) ───────────
#define MAX_EVENTS 30
struct Event {
  String name;
  String city;
  String dateFmt;   // "GG/MM/AAAA"
  int    daysTo;
};
Event  events[MAX_EVENTS];
int    eventCount    = 0;
int    currentEvent  = 0;           // indice evento mostrato
unsigned long eventSlideTimer = 0;  // timer cambio evento (5s)
unsigned long lastFetchTime  = 0;  // timer re-fetch eventi
int fetchTotalSteps = 3;           // 3 mesi da scaricare
int fetchCurrentStep = 0;

// ── Stato app ─────────────────────────────────────────
enum AppState { ST_AP, ST_RUNNING };
AppState appState = ST_AP;

// ── Schermate ─────────────────────────────────────────
// SCR_EVENT può stare molti cicli (uno per evento)
enum Screen { SCR_CARD, SCR_STATS, SCR_EVENTS, SCR_CLOCK, SCR_COUNT };
Screen currentScreen = SCR_CARD;
unsigned long screenTimer = 0;
// ── Blink e scroll ────────────────────────────────────
bool blinkState = true;
unsigned long blinkTimer = 0;

// ══════════════════════════════════════════════════════
//  HASH + GENERAZIONE STATS
// ══════════════════════════════════════════════════════
uint32_t nameHash(const char* s) {
  uint32_t h = 5381;
  while (*s) h = ((h << 5) + h) ^ (uint8_t)(*s++);
  return h;
}
int statFrom(uint32_t seed, int offset, int lo, int hi) {
  uint32_t v = seed * 1664525UL + 1013904223UL + (uint32_t)offset * 22695477UL;
  return lo + (int)((v >> 24) % (hi - lo + 1));
}
int calcAge() {
  // Richiede NTP già sincronizzato
  time_t rawtime = (time_t)timeClient.getEpochTime();
  struct tm* ti  = gmtime(&rawtime);
  String bd = CHAR_BIRTHDAY;
  int bd_d = bd.substring(0,2).toInt();
  int bd_m = bd.substring(3,5).toInt();
  int bd_y = bd.substring(6,10).toInt();
  int age  = (ti->tm_year + 1900) - bd_y;
  if ((ti->tm_mon + 1) < bd_m ||
      ((ti->tm_mon + 1) == bd_m && ti->tm_mday < bd_d)) age--;
  return max(1, age);
}
const char* classes[] = {
  "COSPLAYER","CRAFTER","WARRIOR","MAGE","ROGUE","BARD","PALADIN","RANGER"
};
void generateStats() {
  uint32_t h  = nameHash(CHAR_NAME);
  statStamina = statFrom(h, 1, 20, 99);
  statCraft   = statFrom(h, 2, 20, 99);
  statStyle   = statFrom(h, 3, 20, 99);
  statHype    = statFrom(h, 4, 20, 99);
  charLevel   = calcAge();
  xpMax       = charLevel * 100;
  xpCurrent   = statFrom(h, 6, xpMax / 10, (xpMax * 9) / 10);
  if      (statStyle  > 90) charClass = "COSPLAYER";
  else if (statCraft  > 85) charClass = "CRAFTER";
  else if (statHype   > 90) charClass = "BARD";
  else                      charClass = classes[h % 8];
}

// ══════════════════════════════════════════════════════
//  EEPROM
// ══════════════════════════════════════════════════════
void eeWriteStr(int addr, const String& s, int maxLen) {
  for (int i = 0; i < maxLen; i++)
    EEPROM.write(addr + i, i < (int)s.length() ? s[i] : 0);
}
String eeReadStr(int addr, int maxLen) {
  String s = "";
  for (int i = 0; i < maxLen; i++) {
    char c = (char)EEPROM.read(addr + i);
    if (!c) break;
    s += c;
  }
  return s;
}
bool hasSavedWifi()                           { return EEPROM.read(EE_MAGIC_ADDR) == EE_MAGIC_VAL; }
void saveWifi(const String& s, const String& p) {
  EEPROM.write(EE_MAGIC_ADDR, EE_MAGIC_VAL);
  eeWriteStr(EE_SSID_ADDR, s, 32);
  eeWriteStr(EE_PASS_ADDR, p, 64);
  EEPROM.commit();
}
void clearWifi() { EEPROM.write(EE_MAGIC_ADDR, 0); EEPROM.commit(); }

// ══════════════════════════════════════════════════════
//  HELPER: converte UTF-8 → CP437 per display OLED
//  Gestisce accenti italiani e caratteri comuni
// ══════════════════════════════════════════════════════
void fixAccents(String& s) {
  String out;
  out.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c == 0xC3 && i + 1 < s.length()) {
      uint8_t c2 = (uint8_t)s[i + 1];
      i++;  // salta il secondo byte
      switch (c2) {
        case 0xA0: out += (char)0x85; break; // à
        case 0xA1: out += (char)0xA0; break; // á
        case 0xA8: out += (char)0x8A; break; // è
        case 0xA9: out += (char)0x82; break; // é
        case 0xAC: out += (char)0x8D; break; // ì
        case 0xAD: out += (char)0xA1; break; // í
        case 0xB2: out += (char)0x95; break; // ò
        case 0xB3: out += (char)0xA2; break; // ó
        case 0xB9: out += (char)0x97; break; // ù
        case 0xBA: out += (char)0xA3; break; // ú
        case 0x80: out += (char)0x80; break; // À
        case 0x88: out += (char)0xD4; break; // È (approssimato)
        case 0x89: out += (char)0x90; break; // É
        case 0x8C: out += (char)0xDE; break; // Ì (approssimato)
        case 0x92: out += (char)0xE0; break; // Ò (approssimato)
        case 0x99: out += (char)0xE9; break; // Ù (approssimato)
        case 0xBC: out += (char)0x81; break; // ü
        case 0xB6: out += (char)0x94; break; // ö
        case 0xA4: out += (char)0x84; break; // ä
        default:   out += '?'; break;
      }
    } else if (c == 0xC2 && i + 1 < s.length()) {
      uint8_t c2 = (uint8_t)s[i + 1];
      i++;
      if (c2 == 0xB0) out += (char)0xF8; // °
      else out += '?';
    } else if (c == 0xE2 && i + 2 < s.length()) {
      // Skip 3-byte UTF-8 (smart quotes, dashes, etc.)
      uint8_t c2 = (uint8_t)s[i + 1];
      uint8_t c3 = (uint8_t)s[i + 2];
      i += 2;
      if (c2 == 0x80 && c3 == 0x93) out += '-';       // –
      else if (c2 == 0x80 && c3 == 0x94) out += '-';   // —
      else if (c2 == 0x80 && c3 == 0x99) out += '\'';  // '
      else if (c2 == 0x80 && c3 == 0x9C) out += '"';   // "
      else if (c2 == 0x80 && c3 == 0x9D) out += '"';   // "
      else out += ' ';
    } else if (c < 0x80) {
      out += (char)c;  // ASCII normale
    }
    // Altrimenti skip byte invalido
  }
  s = out;
}

// ══════════════════════════════════════════════════════
//  HELPER: cerca pattern data italiana "DD NomeMese YYYY"
//  Ritorna true e popola d/m/y se trovato
// ══════════════════════════════════════════════════════
static const char* mNamesIt[] = {
  "gennaio","febbraio","marzo","aprile","maggio","giugno",
  "luglio","agosto","settembre","ottobre","novembre","dicembre"
};
bool parseItalianDate(const String& text, int& outD, int& outM, int& outY) {
  String low = text;
  low.toLowerCase();
  for (int mi = 0; mi < 12; mi++) {
    int mPos = low.indexOf(mNamesIt[mi]);
    if (mPos < 0) continue;
    outM = mi + 1;
    // Giorno: cifre prima del nome mese
    String pre = low.substring(0, mPos);
    pre.trim();
    int numEnd = pre.length();
    while (numEnd > 0 && !isDigit(pre[numEnd-1])) numEnd--;
    int numStart = numEnd;
    while (numStart > 0 && isDigit(pre[numStart-1])) numStart--;
    outD = pre.substring(numStart, numEnd).toInt();
    // Anno: cifre dopo il nome mese
    String post = low.substring(mPos + strlen(mNamesIt[mi]));
    post.trim();
    outY = post.toInt();
    if (outD > 0 && outD <= 31 && outY >= 2024) return true;
  }
  return false;
}

// ══════════════════════════════════════════════════════
//  HELPER: rimuove TUTTI i tag HTML da una stringa
// ══════════════════════════════════════════════════════
String stripTags(const String& src) {
  String out;
  out.reserve(src.length());
  bool inTag = false;
  for (unsigned int i = 0; i < src.length(); i++) {
    if (src[i] == '<') { inTag = true; continue; }
    if (src[i] == '>') { inTag = false; continue; }
    if (!inTag) out += src[i];
  }
  out.trim();
  return out;
}

// ══════════════════════════════════════════════════════
//  FETCH EVENTI — scarica e parsa le pagine mese
// ══════════════════════════════════════════════════════
void fetchEvents() {
  eventCount   = 0;
  currentEvent = 0;

  time_t rawNow = (time_t)timeClient.getEpochTime();
  struct tm* ti = gmtime(&rawNow);

  struct tm nowTm = {};
  nowTm.tm_year = ti->tm_year; nowTm.tm_mon = ti->tm_mon; nowTm.tm_mday = ti->tm_mday;
  time_t nowEpoch = mktime(&nowTm);

  const char* monthNames[] = {
    "", "gennaio","febbraio","marzo","aprile","maggio","giugno",
    "luglio","agosto","settembre","ottobre","novembre","dicembre"
  };

  for (int offset = 0; offset <= 2 && eventCount < MAX_EVENTS; offset++) {
    int m = ti->tm_mon + 1 + offset;
    int y = ti->tm_year + 1900;
    if (m > 12) { m -= 12; y++; }

    String url = String("https://www.cosplayersitaliani.it/fiere_comics_categoria/fiere-comics-")
               + monthNames[m] + "-" + String(y) + "/";

    Serial.print(F("\n[fetch] ════════════════════════\n[fetch] GET ")); Serial.println(url);
    fetchCurrentStep = offset;
    drawFetchProgress(monthNames[m]);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20000);

    HTTPClient http;
    http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
      Serial.println(F("[fetch] begin FALLITO"));
      http.end();
      continue;
    }

    // Header DOPO begin()!
    http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"));
    http.addHeader(F("Accept"), F("text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"));
    http.addHeader(F("Accept-Language"), F("it-IT,it;q=0.9,en-US;q=0.8,en;q=0.7"));
    http.addHeader(F("Accept-Encoding"), F("identity"));  // NO gzip!
    http.addHeader(F("Connection"), F("close"));
    http.addHeader(F("Cache-Control"), F("no-cache"));

    int code = http.GET();
    Serial.print(F("[fetch] HTTP code: ")); Serial.println(code);

    if (code != HTTP_CODE_OK) {
      Serial.print(F("[fetch] ERRORE HTTP! code=")); Serial.println(code);
      // Logga un po' del body per capire cosa restituisce
      if (code > 0) {
        String errBody = http.getString().substring(0, 300);
        Serial.print(F("[fetch] Body preview: ")); Serial.println(errBody);
      }
      http.end();
      continue;
    }

    // ── Leggi il body in streaming e parsa ──
    WiFiClient* stream = http.getStreamPtr();
    String line;
    unsigned long fetchStart = millis();

    // State machine per il parser
    String pendingTitle = "";
    String pendingDate  = "";
    String pendingCity  = "";
    int liCount = 0;
    bool debugFirstLines = true;
    int lineNum = 0;

    // Lambda per salvare evento
    auto saveEvent = [&]() {
      if (pendingTitle.length() == 0 || pendingDate.length() == 0) return;
      int ed = 0, em = 0, ey = 0;
      if (!parseItalianDate(pendingDate, ed, em, ey)) {
        Serial.print(F("  [skip] data non parsabile: ")); Serial.println(pendingDate);
        pendingTitle = ""; pendingDate = ""; pendingCity = ""; liCount = 0;
        return;
      }
      if (eventCount >= MAX_EVENTS) return;
      struct tm evTm = {};
      evTm.tm_year = ey-1900; evTm.tm_mon = em-1; evTm.tm_mday = ed;
      time_t evEpoch = mktime(&evTm);
      int dt = (int)((evEpoch - nowEpoch) / 86400);
      if (dt >= 0) {
        char buf[11];
        sprintf(buf, "%02d/%02d/%04d", ed, em, ey);
        pendingTitle.replace("&amp;", "&");
        pendingTitle.replace("&#8211;", "-");
        pendingTitle.replace("&#8217;", "'");
        pendingTitle.replace("&ndash;", "-");
        pendingTitle.replace("&#038;", "&");
        pendingTitle.replace("&#8230;", "...");
        if (pendingCity.length() == 0) pendingCity = "---";
        fixAccents(pendingTitle);
        fixAccents(pendingCity);
        events[eventCount] = { pendingTitle, pendingCity, String(buf), dt };
        eventCount++;
        Serial.print(F("  [OK] ")); Serial.print(pendingTitle);
        Serial.print(F(" @ ")); Serial.print(pendingCity);
        Serial.print(F(" | ")); Serial.println(buf);
      }
      pendingTitle = ""; pendingDate = ""; pendingCity = ""; liCount = 0;
    };

    while (http.connected() && (millis() - fetchStart < 20000)) {
      if (!stream->available()) { delay(1); continue; }
      char ch = stream->read();
      if (ch == '\n') {
        line.trim();
        lineNum++;

        // Debug: prime 20 righe significative per capire la struttura
        if (debugFirstLines && lineNum <= 50 && line.length() > 5) {
          Serial.print(F("[html L")); Serial.print(lineNum); Serial.print(F("] "));
          Serial.println(line.substring(0, 150));
        }
        if (lineNum > 50) debugFirstLines = false;

        // ── STRATEGIA 1: cerca <h2> o <h3> con titolo evento ──
        if (line.indexOf("<h2") >= 0 || line.indexOf("<h3") >= 0) {
          // Salva evento precedente se pendente
          if (pendingTitle.length() > 0 && pendingDate.length() > 0) saveEvent();

          String tag = (line.indexOf("<h2") >= 0) ? "</h2>" : "</h3>";
          int tOpen = (line.indexOf("<h2") >= 0) ? line.indexOf("<h2") : line.indexOf("<h3");
          int tClose = line.indexOf(tag);
          if (tOpen >= 0 && tClose > tOpen) {
            String clean = stripTags(line.substring(tOpen, tClose));
            if (clean.length() > 2 && clean.length() < 60 &&
                clean.indexOf("Fiere") < 0 && clean.indexOf("cosplayersitaliani") < 0 &&
                clean.indexOf("Cookie") < 0 && clean.indexOf("Event") < 0 &&
                clean.indexOf("Calendario") < 0 && clean.indexOf("Tutti") < 0) {
              pendingTitle = clean;
              pendingDate  = "";
              pendingCity  = "";
              liCount      = 0;
              Serial.print(F("  [h2/h3] titolo: ")); Serial.println(pendingTitle);
            }
          }
        }

        // ── STRATEGIA 2: cerca <li> dopo un titolo ──
        if (pendingTitle.length() > 0 && line.indexOf("<li") >= 0) {
          int s = line.indexOf("<li");
          int gt = line.indexOf('>', s);
          if (gt < 0) gt = s + 3;
          int e2 = line.indexOf("</li>", gt);
          if (e2 < 0) e2 = line.length();
          String val = stripTags(line.substring(gt + 1, e2));
          if (val.length() > 0) {
            liCount++;
            Serial.print(F("    [li #")); Serial.print(liCount);
            Serial.print(F("] ")); Serial.println(val);
            // Primo li con data = data inizio
            int td, tm2, ty;
            if (liCount <= 2 && pendingDate.length() == 0 && parseItalianDate(val, td, tm2, ty)) {
              pendingDate = val;
            }
            // Li con testo non-data dopo la data = probabile luogo/città
            else if (pendingDate.length() > 0 && pendingCity.length() == 0 &&
                     !parseItalianDate(val, td, tm2, ty) &&
                     val.indexOf("Info") < 0 && val.length() > 2 && val.length() < 60) {
              pendingCity = val;
            }
            // Dopo un po' di li, salva quello che abbiamo
            if (liCount >= 4 && pendingDate.length() > 0) {
              saveEvent();
            }
          }
        }

        // ── STRATEGIA 3: cerca date e città in testo generico ──
        // Il sito usa spesso: "06 Giugno 2026 · Luogo · Regione · Info"
        // oppure campi su righe separate senza <li>
        if (pendingTitle.length() > 0) {
          String stripped = stripTags(line);
          stripped.trim();

          if (stripped.length() >= 3) {
            // Controlla se la riga contiene separatori ·
            bool hasDot = (stripped.indexOf(F("\xC2\xB7")) >= 0 || stripped.indexOf(F("·")) >= 0);

            if (hasDot) {
              // Parsa campi separati da ·
              stripped.replace(F("\xC2\xB7"), F("|"));
              stripped.replace(F("·"), F("|"));
              int segStart = 0;
              int segIdx = 0;
              while (segStart < (int)stripped.length() && segIdx < 6) {
                int segEnd = stripped.indexOf('|', segStart);
                if (segEnd < 0) segEnd = stripped.length();
                String seg = stripped.substring(segStart, segEnd);
                seg.trim();
                segStart = segEnd + 1;
                if (seg.length() < 2) { segIdx++; continue; }

                int td, tm2, ty;
                if (parseItalianDate(seg, td, tm2, ty)) {
                  if (pendingDate.length() == 0) {
                    pendingDate = seg;
                    Serial.print(F("    [dot-date] ")); Serial.println(seg);
                  }
                } else if (seg.indexOf(F("Info")) < 0 && seg.length() > 2 && seg.length() < 50) {
                  if (pendingCity.length() == 0 && pendingDate.length() > 0) {
                    pendingCity = seg;
                    Serial.print(F("    [dot-city] ")); Serial.println(seg);
                  }
                }
                segIdx++;
              }
            } else {
              // Riga senza · — fallback singolo campo
              int td, tm2, ty;
              if (pendingDate.length() == 0 && parseItalianDate(stripped, td, tm2, ty)) {
                pendingDate = stripped;
                Serial.print(F("    [date-fallback] ")); Serial.println(pendingDate);
              }
              else if (pendingDate.length() > 0 && pendingCity.length() == 0 &&
                       !parseItalianDate(stripped, td, tm2, ty) &&
                       stripped.indexOf(F("Info")) < 0 &&
                       stripped.indexOf(F("Fiere")) < 0 &&
                       stripped.indexOf(F("http")) < 0 &&
                       stripped.indexOf(F("cookie")) < 0 &&
                       stripped.indexOf(F("Guida")) < 0 &&
                       stripped.length() > 2 && stripped.length() < 50) {
                pendingCity = stripped;
                Serial.print(F("    [city-fallback] ")); Serial.println(pendingCity);
              }
            }
          }
        }

        line = "";
      } else if (ch != '\r') {
        if (line.length() < 600) line += ch;
      }
    }
    // Salva ultimo evento pendente
    if (pendingTitle.length() > 0 && pendingDate.length() > 0) saveEvent();

    http.end();
    Serial.print(F("[fetch] fine mese ")); Serial.print(m);
    Serial.print(F("/")); Serial.print(y);
    Serial.print(F(": ")); Serial.print(eventCount); Serial.println(F(" eventi totali"));
  }

  Serial.print(F("\n[fetch] ═══ TOTALE FINALE: ")); Serial.print(eventCount); Serial.println(F(" eventi ═══"));
  fetchCurrentStep = fetchTotalSteps;
  drawFetchProgress("Completato!");
  delay(800);

  if (eventCount == 0) {
    events[0] = { "Nessun evento", "prossimo mese", "", -1 };
    eventCount = 1;
  }
}


void drawFetchProgress(const char* msg) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.drawRect(0, 0, 128, 64, WHITE);

  display.setTextSize(1);
  display.setCursor(18, 4);
  display.print(F("CARICAMENTO"));
  sep(14);

  // Nome mese corrente
  display.setCursor(4, 20);
  display.print(msg);

  // Barra progress
  int pct = (fetchCurrentStep * 100) / max(1, fetchTotalSteps);
  display.drawRect(4, 34, 120, 14, WHITE);
  int fill = map(pct, 0, 100, 0, 118);
  if (fill > 0) display.fillRect(5, 35, fill, 12, WHITE);

  // Percentuale
  char pctBuf[5];
  sprintf(pctBuf, "%d%%", pct);
  display.setCursor(52, 52);
  display.print(pctBuf);

  display.display();
}


void drawBoot(const char* msg, int pct) {
  display.clearDisplay();
  display.setTextColor(WHITE);

  // Bordo
  display.drawRect(0, 0, 128, 64, WHITE);

  // Titolo
  display.setTextSize(1);
  display.setCursor(14, 4);
  display.print(F("COSPLAY RPG v1.0"));
  sep(14);

  // Messaggio stato
  display.setTextSize(1);
  display.setCursor(4, 20);
  display.print(msg);

  // Barra progress
  display.drawRect(4, 38, 120, 12, WHITE);
  int fill = map(pct, 0, 100, 0, 118);
  if (fill > 0) display.fillRect(5, 39, fill, 10, WHITE);

  // Percentuale
  display.setCursor(54, 53);
  display.print(pct);
  display.print(F("%"));

  display.display();
}

// ══════════════════════════════════════════════════════
//  SCHERMATA: AP setup
// ══════════════════════════════════════════════════════
void drawAPScreen() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.drawRect(0, 0, 128, 64, WHITE);

  display.setTextSize(1);

  // Titolo
  display.setCursor(22, 3);
  display.print(F("WIFI SETUP"));
  sep(13);

  // Istruzioni
  display.setCursor(4, 17);
  display.print(F("1. Connettiti a:"));

  display.setCursor(4, 28);
  display.print(AP_SSID);

  display.setCursor(4, 39);
  display.print(F("2. Apri il browser"));

  display.setCursor(4, 50);
  display.print(F("   192.168.4.1"));

  display.display();
}

// ══════════════════════════════════════════════════════
//  SCHERMATA: connessione in corso
// ══════════════════════════════════════════════════════
void drawConnecting(int tick) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.drawRect(0, 0, 128, 64, WHITE);

  display.setTextSize(1);
  display.setCursor(16, 4);
  display.print(F("CONNESSIONE..."));
  sep(14);

  display.setCursor(4, 28);
  // Animazione puntini
  int dots = (tick % 4) + 1;
  for (int i = 0; i < dots; i++) display.print(F(". "));

  display.display();
}

// ══════════════════════════════════════════════════════
//  SCHERMATA 1: RPG CARD
// ══════════════════════════════════════════════════════
void drawCard() {
  display.setTextColor(WHITE);

  // Bordo esterno
  display.drawRect(0, 0, 128, 64, WHITE);

  // Riga 1: classe (sx) e livello (dx) — y=4
  display.setTextSize(1);
  display.setCursor(3, 4);
  display.print(charClass.substring(0, 9));

  // Livello allineato a destra — sprintf evita conversione float
  char lvlBuf[6];
  sprintf(lvlBuf, "LV%d", (int)charLevel);
  display.setCursor(128 - (int)strlen(lvlBuf) * 6 - 3, 4);
  display.print(lvlBuf);

  sep(14);

  // Riga 2: nome grande centrato — y=18
  display.setTextSize(2);
  int nameW = strlen(CHAR_NAME) * 12;
  display.setCursor(max(2, (128 - nameW) / 2), 18);
  display.print(CHAR_NAME);

  sep(36);

  // Riga 3: barra XP — y=40
  display.setTextSize(1);
  display.setCursor(3, 40);
  display.print(F("XP"));
  drawBar(18, 39, 106, 9, map(xpCurrent, 0, xpMax, 0, 100));

  sep(51);

  // Riga 4: due stat in fondo — y=55
  display.setCursor(3, 55);
  display.print(F("STM"));
  drawBar(24, 54, 44, 8, statStamina);

  display.setCursor(74, 55);
  display.print(F("STL"));
  drawBar(95, 54, 30, 8, statStyle);

}

// ══════════════════════════════════════════════════════
//  SCHERMATA 2: STATS dettaglio
// ══════════════════════════════════════════════════════
void drawStats() {
  display.setTextColor(WHITE);
  display.drawRect(0, 0, 128, 64, WHITE);

  display.setTextSize(1);
  display.setCursor(36, 4);
  display.print(F("[ STATS ]"));
  sep(14);

  // 4 righe, ciascuna con: label | barra | valore
  // Spazio: da y=18 a y=58, 4 righe da 10px
  struct { const char* label; int val; } rows[] = {
    { "STAMINA", statStamina },
    { "CRAFT  ", statCraft   },
    { "STYLE  ", statStyle   },
    { "HYPE   ", statHype    },
  };

  // 4 righe in 50px (y 14 → 64): ogni riga alta 12px
  // barY: 15, 27, 39, 51  — stando tutti dentro lo schermo
  for (int i = 0; i < 4; i++) {
    int barY  = 15 + i * 12;
    int textY = barY + 1;

    display.setCursor(3, textY);
    display.print(rows[i].label);  // 7 char * 6px = 42px

    drawBar(46, barY, 60, 9, rows[i].val);

    char valBuf[4];
    sprintf(valBuf, "%3d", rows[i].val);
    display.setCursor(109, textY);
    display.print(valBuf);
  }
}

// ══════════════════════════════════════════════════════
//  SCHERMATA 3: CAROUSEL EVENTI
//  Un evento alla volta, cambia ogni 5s
// ══════════════════════════════════════════════════════
void drawEventCard() {
  display.setTextColor(WHITE);
  display.drawRect(0, 0, 128, 64, WHITE);

  Event& ev = events[currentEvent];
  bool urgent = (ev.daysTo >= 0 && ev.daysTo <= 7);

  // ── Header ──
  if (urgent && blinkState) {
    display.fillRect(0, 0, 128, 13, WHITE);
    display.setTextColor(BLACK);
  }

  display.setTextSize(1);
  // "1/5" a sinistra
  char hdr[8];
  sprintf(hdr, "%d/%d", currentEvent + 1, eventCount);
  display.setCursor(3, 3);
  display.print(hdr);

  // Indicatore urgenza: "HOT!" al centro — 4 char * 6px = 24px
  if (urgent) {
    display.setCursor(52, 3);
    display.print(F("HOT!"));
  }

  // Countdown compatto a destra
  if (ev.dateFmt.length()) {
    char cdBuf[10];
    if      (ev.daysTo == 0) strcpy(cdBuf, "OGGI!");
    else if (ev.daysTo == 1) strcpy(cdBuf, "DOMANI");
    else if (ev.daysTo > 1)  sprintf(cdBuf, "-%dgg", ev.daysTo);
    else                     cdBuf[0] = 0;
    if (cdBuf[0]) {
      int cdW = strlen(cdBuf) * 6;
      display.setCursor(125 - cdW, 3);
      display.print(cdBuf);
    }
  }

  display.setTextColor(WHITE);
  sep(13);

  // ── Nome evento — 2 righe max ──
  display.setTextSize(1);
  String name = ev.name;
  if ((int)name.length() <= 21) {
    display.setCursor(3, 16);
    display.print(name);
  } else {
    // Prima riga: tronca a spazio se possibile
    int cut = 21;
    for (int i = 20; i >= 15; i--) {
      if (name[i] == ' ') { cut = i; break; }
    }
    display.setCursor(3, 16);
    display.print(name.substring(0, cut));
    display.setCursor(3, 25);
    display.print(name.substring(cut + (name[cut] == ' ' ? 1 : 0), cut + 21));
  }

  sep(35);

  // ── Luogo ──
  display.setCursor(3, 38);
  if (ev.city.length() > 0 && ev.city != "---") {
    display.print(ev.city.substring(0, 21));
  } else {
    display.print(F("---"));
  }

  sep(49);

  // ── Data ──
  display.setCursor(3, 53);
  if (ev.dateFmt.length()) {
    display.print(ev.dateFmt);
  } else {
    display.print(F("nessun evento"));
  }
}

// ══════════════════════════════════════════════════════
//  SCHERMATA 4: OROLOGIO
// ══════════════════════════════════════════════════════
void drawClock() {
  display.setTextColor(WHITE);

  if (WiFi.status() != WL_CONNECTED) {
    display.setTextSize(1);
    display.drawRect(0, 0, 128, 64, WHITE);
    display.setCursor(22, 28);
    display.print(F("No WiFi  :("));
    return;
  }

  time_t rawtime = (time_t)timeClient.getEpochTime();
  struct tm* ti  = gmtime(&rawtime);

  // Ora grande con blink dei due punti
  char timeBuf[6];
  sprintf(timeBuf, blinkState ? "%02d:%02d" : "%02d %02d", ti->tm_hour, ti->tm_min);

  display.setTextSize(3);
  // Centra: ogni char è 18px, 5 char = 90px
  display.setCursor((128 - 90) / 2, 8);
  display.print(timeBuf);

  sep(42);

  // Data e giorno
  char dateBuf[11];
  sprintf(dateBuf, "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
  const char* days[] = { "DOM","LUN","MAR","MER","GIO","VEN","SAB" };

  display.setTextSize(1);
  display.setCursor(3, 47);
  display.print(days[ti->tm_wday]);
  display.print(F("  "));
  display.print(dateBuf);

  // Puntino sync wifi
  display.fillCircle(124, 60, 2, WHITE);
}

// ══════════════════════════════════════════════════════
//  AP CAPTIVE PORTAL
// ══════════════════════════════════════════════════════
String buildPage(const String& msg = "") {
  return R"(<!DOCTYPE html><html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>CosplayRPG Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0a0a0f;color:#00ff88;font-family:monospace;
     display:flex;align-items:center;justify-content:center;min-height:100vh}
.box{background:#111118;border:1px solid #1e3d2a;border-radius:8px;
     padding:32px 24px;width:92%;max-width:360px}
.name{text-align:center;font-size:26px;letter-spacing:4px;color:#00ff88;margin-bottom:4px}
.sub{text-align:center;font-size:11px;color:#445544;margin-bottom:28px;letter-spacing:1px}
h2{color:#ffaa00;letter-spacing:3px;font-size:12px;margin-bottom:16px}
label{display:block;font-size:11px;color:#445544;letter-spacing:1px;margin-bottom:5px}
input{width:100%;background:#1a1a24;border:1px solid #1e3d2a;border-radius:4px;
      color:#00ff88;font-family:monospace;font-size:15px;padding:10px 12px;margin-bottom:16px}
button{width:100%;background:transparent;border:1px solid #00ff88;color:#00ff88;
       font-family:monospace;font-size:13px;letter-spacing:2px;padding:11px;border-radius:4px;cursor:pointer}
.err{color:#ff4455;font-size:12px;margin-bottom:14px;text-align:center;padding:8px;
     border:1px solid #ff445533;border-radius:4px;background:#ff44550a}
</style></head><body><div class='box'>
<div class='name'>)" + String(CHAR_NAME) + R"(</div>
<div class='sub'>CosplayRPG &mdash; HexLions</div>
<h2>CONFIGURAZIONE WIFI</h2>)"
  + (msg.length() ? "<div class='err'>" + msg + "</div>" : "")
  + R"(<form method='POST' action='/save'>
<label>NOME RETE (SSID)</label>
<input type='text' name='ssid' placeholder='NomeRete' autocomplete='off' autocapitalize='none'>
<label>PASSWORD</label>
<input type='password' name='pass' placeholder='Password'>
<button type='submit'>&#9654; CONNETTI</button>
</form></div></body></html>)";
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  auto sendPage = [](){ server.send(200, "text/html", buildPage()); };
  server.on("/",                    HTTP_GET, sendPage);
  server.on("/generate_204",        HTTP_GET, sendPage);
  server.on("/hotspot-detect.html", HTTP_GET, sendPage);
  server.onNotFound(sendPage);

  server.on("/save", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (!ssid.length()) {
      server.send(200, "text/html", buildPage("Inserisci il nome della rete."));
      return;
    }
    server.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<style>body{background:#0a0a0f;color:#00ff88;font-family:monospace;"
      "display:flex;align-items:center;justify-content:center;min-height:100vh;"
      "text-align:center;padding:20px}</style></head><body>"
      "<div><div style='font-size:22px;color:#ffaa00;letter-spacing:3px;"
      "margin-bottom:16px'>CONNESSIONE...</div>"
      "<div style='color:#445544;font-size:13px'>Guarda il display!</div>"
      "</div></body></html>");

    delay(200);
    server.stop();
    WiFi.softAPdisconnect(true);
    delay(300);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 24 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500); drawConnecting(i);
    }

    if (WiFi.status() == WL_CONNECTED) {
      saveWifi(ssid, pass);
      appState = ST_RUNNING;
      drawBoot("NTP...", 60);
      timeClient.begin(); timeClient.update();
      generateStats();
      drawBoot("FETCH EVENTI...", 80);
      fetchEvents();
      lastFetchTime = millis();
      drawBoot("PRONTO!", 100);
      delay(700);
      screenTimer     = millis();
      eventSlideTimer = millis();
    } else {
      clearWifi();
      WiFi.mode(WIFI_AP);
      WiFi.softAP(AP_SSID, AP_PASSWORD);
      server.begin();
      drawAPScreen();
    }
  });

  server.begin();
  appState = ST_AP;
  drawAPScreen();
}

// ══════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n=== CosplayRPG boot ==="));
  EEPROM.begin(128);
  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.setRotation(2);
  display.cp437(true);   // charset CP437 per accenti italiani
  display.clearDisplay();
  display.display();

  drawBoot("INIT...", 10);

  if (hasSavedWifi()) {
    String ssid = eeReadStr(EE_SSID_ADDR, 32);
    String pass = eeReadStr(EE_PASS_ADDR, 64);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 24 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500); drawConnecting(i);
    }
    if (WiFi.status() == WL_CONNECTED) {
      appState = ST_RUNNING;
      drawBoot("NTP...", 60);
      timeClient.begin(); timeClient.update();
      generateStats();
      drawBoot("FETCH EVENTI...", 80);
      fetchEvents();
      lastFetchTime = millis();
      drawBoot("PRONTO!", 100);
      delay(700);
    } else {
      clearWifi();
      startAP();
    }
  } else {
    startAP();
  }

  screenTimer     = millis();
  eventSlideTimer = millis();
}

// ══════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════
void loop() {
  if (appState == ST_AP) {
    server.handleClient();
    return;
  }

  unsigned long now = millis();

  if (now - blinkTimer > 500) { blinkState = !blinkState; blinkTimer = now; }

  timeClient.update();

  unsigned long sDur = (currentScreen == SCR_EVENTS)
    ? max(5000UL, (unsigned long)eventCount * 5000UL)
    : 5000UL;
  if (now - screenTimer > sDur) {
    currentScreen   = (Screen)((currentScreen + 1) % SCR_COUNT);
    screenTimer     = now;
    currentEvent    = 0;
    eventSlideTimer = now;
  }

  if (currentScreen == SCR_EVENTS && eventCount > 1 && now - eventSlideTimer > 5000) {
    currentEvent    = (currentEvent + 1) % eventCount;
    eventSlideTimer = now;
  }

  if (WiFi.status() == WL_CONNECTED &&
      now - lastFetchTime > (unsigned long)FETCH_INTERVAL * 60000UL) {
    fetchEvents();
    lastFetchTime = now;
  }

  display.clearDisplay();
  switch (currentScreen) {
    case SCR_CARD:   drawCard();      break;
    case SCR_STATS:  drawStats();     break;
    case SCR_EVENTS: drawEventCard(); break;
    case SCR_CLOCK:  drawClock();     break;
    default: break;
  }
  display.display();
  delay(40);
}
