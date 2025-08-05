#include <FS.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <ESP32Time.h>
#include <NetworkClient.h>
#include <Timezone.h>
#include <FastLED.h>
#include "FastLED_RGBW.h"

Preferences prefs;

String WiFiSSID = "";
String WiFiPassword = "";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset = 3600;

// Gives GMT offset taking daylight offset into account (in minutes)
TimeChangeRule CEST = { "CEST", Last, Sun, Mar, 2, 120 };  // Central European Summer Time
TimeChangeRule CET = { "CET", Last, Sun, Oct, 3, 60 };     // Central European Standard Time
Timezone CE(CEST, CET);

#define HOURS_TENS_PIN 13
#define HOURS_UNITS_PIN 27
#define COLON_PIN 26
#define MINUTES_TENS_PIN 33
#define MINUTES_UNITS_PIN 32

/* FastLED_RGBW
 * 
 * Hack to enable SK6812 RGBW strips to work with FastLED.
 *
 * Original code by Jim Bumgardner (http://krazydad.com).
 * Modified by David Madison (http://partsnotincluded.com).
 * 
*/
CRGBW hoursTensLeds[7];
CRGB *hoursTensLedsRGB = (CRGB *)&hoursTensLeds[0];
CRGBW hoursUnitsLeds[7];
CRGB *hoursUnitsLedsRGB = (CRGB *)&hoursUnitsLeds[0];
CRGBW colonLeds[2];
CRGB *colonLedsRGB = (CRGB *)&colonLeds[0];
bool colonState = true;
CRGBW minutesTensLeds[7];
CRGB *minutesTensLedsRGB = (CRGB *)&minutesTensLeds[0];
CRGBW minutesUnitsLeds[7];
CRGB *minutesUnitsLedsRGB = (CRGB *)&minutesUnitsLeds[0];

CRGB color = 0x00FFFF;
CRGB hourColor = 0x00FFFF;
CRGB colonColor = 0x00FFFF;
CRGB minuteColor = 0x00FFFF;
uint8_t brightness = 150;

// Represents the 7 segment of 1 digit, starting from the center one going in a spiral CCW
// Last character is blank
bool DIGITS_CHARACTERS[11][7] = {
  {0, 1, 1, 1, 1, 1, 1},
  {0, 1, 0, 0, 0, 0, 1},
  {1, 1, 1, 0, 1, 1, 0},
  {1, 1, 1, 0, 0, 1, 1},
  {1, 1, 0, 1, 0, 0, 1},
  {1, 0, 1, 1, 0, 1, 1},
  {1, 0, 1, 1, 1, 1, 1},
  {0, 1, 1, 0, 0, 0, 1},
  {1, 1, 1, 1, 1, 1, 1},
  {1, 1, 1, 1, 0, 1, 1},
  {0, 0, 0, 0, 0, 0, 0}
};

// Web Server
const String HOUR_COLOR_PLACEHOLDER = "%HCOLOR%";
const String COLON_COLOR_PLACEHOLDER = "%CCOLOR%";
const String MINUTE_COLOR_PLACEHOLDER = "%MCOLOR%";
String hourColorString = "00FFFF";
String colonColorString = "00FFFF";
String minuteColorString = "00FFFF";

const String SSID_PLACEHOLDER = "%SSID%";
const String PASS_PLACEHOLDER = "%PASS%";
const String BRIGHTNESS_PLACEHOLDER = "%BRIGHT%";


// Server at port 80, standard for TTCP server
NetworkServer webServer(80);
bool isServerOnline = false;

bool restartScheluded = false;

void loadSettings(String& ssid, String& pass, CRGB &hour, CRGB &colon, CRGB &minute, uint8_t &brightness);
bool startWebServer();

void setup() {
  Serial.begin(115200);
  Serial.println("Clock Starting.");

  Serial.println("Loading preferences");
  loadSettings(WiFiSSID, WiFiPassword, hourColor, colonColor, minuteColor, brightness);

  Serial.println("Initiating FastLED");
  FastLED.addLeds<WS2812B, HOURS_TENS_PIN, RGB>(hoursTensLedsRGB, getRGBWsize(7));
  FastLED.addLeds<WS2812B, HOURS_UNITS_PIN, RGB>(hoursUnitsLedsRGB, getRGBWsize(7));
  FastLED.addLeds<WS2812B, COLON_PIN, RGB>(colonLedsRGB, getRGBWsize(2));
  FastLED.addLeds<WS2812B, MINUTES_TENS_PIN, RGB>(minutesTensLedsRGB, getRGBWsize(7));
  FastLED.addLeds<WS2812B, MINUTES_UNITS_PIN, RGB>(minutesUnitsLedsRGB, getRGBWsize(7));
  FastLED.setBrightness(brightness);
  FastLED.show();

  Serial.println("Connecting to WiFi");
  // We allow for empty passwords but not for empty SSID
  WiFi.mode(WIFI_STA);
  if (WiFiSSID != "") {
    int retry = 0;
    WiFi.begin(WiFiSSID, WiFiPassword);
    while (WiFi.status() != WL_CONNECTED && retry < 50) {
      delay(500);
      Serial.print(".");
      retry++;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    IPAddress ip = WiFi.localIP();
    String ipString = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    Serial.println("IP Address: " + ipString);

    // Using UTC time to then convert to local time
    Serial.println("Syncing time with NTP server");
    configTime(0, 0, ntpServer);

    // Wait for time sync
    time_t now = time(nullptr);
    int retry = 0;
    while (now < 8 * 3600 * 2 && retry < 30) {
      delay(500);
      Serial.print(".");
      now = time(nullptr);
      retry++;
    }
    Serial.println();

    if (now < 8 * 3600 * 2) {
      Serial.println("NTP sync failed!");
    } else {
      Serial.println("Time synced!");
    }
  } else {
    Serial.println("WiFi not connected.\nLaunching Access Point");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP Clock", "");
    IPAddress ip = WiFi.softAPIP();
    String ipString = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    Serial.println("Started AP\nIP Address: " + ipString);
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  if (!startWebServer()) {
    Serial.println("Couldn't start web server.");
  }

  updateDisplayTime();
}

void saveWiFiCredentials(String &ssid, String& pass) {
  prefs.begin("clock", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void saveULong(const char *key, CRGB& value) {
  prefs.begin("clock", false);
  prefs.putULong(key, (unsigned long) value);
  prefs.end();
}

void saveBrightness(uint8_t& bright) {
  prefs.begin("clock", false);
  prefs.putInt("bright", bright);
  prefs.end();
}

void loadSettings(String& ssid, String& pass, CRGB &hour, CRGB &colon, CRGB &minute, uint8_t &bright) {
  prefs.begin("clock", true);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  hour = prefs.getULong("hour", 0x00FFFF);
  colon = prefs.getULong("colon", 0x00FFFF);
  minute = prefs.getULong("minute", 0x00FFFF);
  bright = prefs.getInt("bright", 100);
  prefs.end();
}

bool startWebServer() {
  webServer.begin();
  Serial.println("TCP server started");

  isServerOnline = true;
  return true;
}

void loop() {
  // Respond to web requests
  EVERY_N_MILLISECONDS(3) {
    if (isServerOnline) {
      NetworkClient client = webServer.accept();
      if (!client) {
        return;
      }
      Serial.println("");
      Serial.println("New client");

      while (client.connected() && !client.available()) {
        delay(1);
      }

      String req = client.readStringUntil('\r');

      int addr_start = req.indexOf(' ');
      int addr_end = req.indexOf(' ', addr_start + 1);
      if (addr_start == -1 || addr_end == -1) {
        Serial.print("Invalid request: ");
        Serial.println(req);
        return;
      }
      req = req.substring(addr_start + 1, addr_end);
      Serial.print("Request: ");
      Serial.println(req);

      String response = handleRequests(req);

      client.print(response);

      client.stop();
      Serial.println("Done with client");

      if (restartScheluded) {}
        restartScheluded = false;
        Serial.println("Settings changed, restarting...");
        restart();
      }
    }
  }

  EVERY_N_MILLISECONDS(500) {
    updateColon();
  }

  EVERY_N_SECONDS(10) {
    updateDisplayTime();
  }
}

String handleRequests(String request) {
  if (request == "/") {
    Serial.println("Sending 200");
    return getHomePage();
  } else if (request == "/settings") {
    Serial.println("Sending 200");
    return getSettingsPage();
  } else if (request.startsWith("/c?")) {
    // There should be 2 parameters to the request: Zone, COlor
    // Respectfully represented by z (int), c (String)
    int zone = 0;
    int zone_start = request.indexOf("z=");
    String zoneString = request.substring(zone_start + 2, zone_start + 4);
    if (!isDigit(zoneString[0])) {
      Serial.println("Sending 400");
      return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nZone must be an integer";
    }
    zone = zoneString.toInt();
    if (zone < 0 || zone > 2) {
      Serial.println("Sending 400");
      return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nZone must be between 0 and 2 (included)";
    }

    int color_start = request.indexOf("c=");
    String colorString = request.substring(color_start + 2);
    char charBuf[7];
    colorString.toCharArray(charBuf, 7);
    if (zone == 0) {
      hourColor = strtoul(charBuf, nullptr, 16);
      hourColorString = colorString;
      saveULong("hour", hourColor);
    } else if (zone == 1) {
      colonColor = strtoul(charBuf, nullptr, 16);
      colonColorString = colorString;
      saveULong("colon", colonColor);
    } else if (zone == 2) {
      minuteColor = strtoul(charBuf, nullptr, 16);
      minuteColorString = colorString;
      saveULong("minute", minuteColor);
    }

    Serial.println("Sending 200");
    return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nColor changed successfully";
  } else if (request.startWith("/b") {
    // Request should contain only one argument: b with an integer between 0 and 255 (inclusive)
    int brightnessStart = request.indexOf("b=");
    String brightnessString = request.substring(brightnessStart + 2);
    for (int i = 0;i < brightnessString.length();i++) {
      if (!isDigit(brightnessString[i])) {
        Serial.println("Sending 400");
        return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nBrightness must be an integer";
      }
    }

    uint8_t bright = brightnessString.toInt();
    if (bright < 0 || bright > 255) {
      Serial.println("Sending 400");
      return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nZone must be between 0 and 255 (included)";
    }
    // Applying to global variable
    brightness = bright;

    // Saving to Preferences
    save

    Serial.println("Sending 200");
    return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nBrightness changed successfully";
  } else if (request.startsWith("/s")) {
    // There can be 2 parameters: s (ssid), p (password)
    int ssid_start = request.indexOf("s=");
    int ssid_end = request.indexOf("&p");
    WiFiSSID = urlDecode(request.substring(ssid_start + 2, ssid_end));

    int pass_start = request.indexOf("p=");
    WiFiPassword = urlDecode(request.substring(pass_start + 2);

    // Save settings to EEPROM
    saveWiFiCredentials(WiFiSSID, WiFiPassword);
    restartScheluded = true;

    Serial.println("Sending 200");
    return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection:\r\n\r\nSettings changed, restarting...";
  } else if (request == "/clear") {
    prefs.begin("clock", false);
    prefs.clear();
    prefs.end();

    restartScheluded = true;

    Serial.println("Sending 200");
    return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection:\r\n\r\nSettings cleared, restarting...";
  } else if (request == "/favicon.png") {
    File file = SPIFFS.open("/favicon.png", "r");
    if (!file) {
      Serial.println("Sending 404");
      return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFavicon Not Found";
    }

    String response = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " + String(file.size()) + "\r\nConection: close\r\n\r\n";

    while(file.available()) {
      response += (char)file.read();
    }

    file.close();
    Serial.println("Sending 200");
    return response;
  }
  Serial.println("Sending 404");
  return "HTTP/1.1 404 Not Found\r\n\r\n";
}

String getHomePage() {
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile Not Found";
  }

  String content = file.readString();
  file.close();

  String response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
  content.replace(HOUR_COLOR_PLACEHOLDER, '#' + hourColorString);
  content.replace(COLON_COLOR_PLACEHOLDER, '#' + colonColorString);
  content.replace(MINUTE_COLOR_PLACEHOLDER, '#' + minuteColorString);
  content.replace(BRIGHTNESS_PLACEHOLDER, String(brightness));
  response += content;

  return response;
}

String getSettingsPage() {
  File file = SPIFFS.open("/settings.html", "r");
  if (!file) {
    return "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
  }

  String content = file.readString();
  file.close();

  String response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
  content.replace(SSID_PLACEHOLDER, WiFiSSID);
  content.replace(PASS_PLACEHOLDER, WiFiPassword);
  response += content;

  return response;
}

String urlDecode(const String& input) {
  String decoded = "";
  char temp[] = "0x00";
  unsigned int len = input.length();
  unsigned int i = 0;
  while (i < len) {
    char c = input.charAt(i);
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%' && i + 2 < len) {
      temp[2] = input.charAt(i + 1);
      temp[3] = input.charAt(i + 2);
      decoded += (char)strtol(temp, NULL, 16);
      i += 2;
    } else {
      decoded += c;
    }
    ++i;
  }
  return decoded;
}

void restart() {
  Serial.println("Disabling WiFi");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.println("Restarting");
  ESP.restart();
}

void updateColon() {
  bool on[2] = {1, 1};
  bool off[2] = {0, 0};
  displayCharacterToLeds((colonState ? on : off), colonLeds, colonColor, 2);
  FastLED.show();
  colonState = !colonState;
}

void updateDisplayTime() {
  time_t raw;
  time(&raw);

  raw = CE.toLocal(raw);
  struct tm timeinfo;
  localtime_r(&raw, &timeinfo);
  
  int hours = timeinfo.tm_hour;
  int minutes = timeinfo.tm_min;

  // We don't display 0 for the tens if it's less than 10
  displayCharacterToLeds(DIGITS_CHARACTERS[hours < 10 ? 10 : hours / 10], hoursTensLeds, hourColor, 7);
  displayCharacterToLeds(DIGITS_CHARACTERS[int(hours % 10)], hoursUnitsLeds, hourColor, 7);
  displayCharacterToLeds(DIGITS_CHARACTERS[minutes / 10], minutesTensLeds, minuteColor, 7);
  displayCharacterToLeds(DIGITS_CHARACTERS[int(minutes % 10)], minutesUnitsLeds, minuteColor, 7);

  FastLED.show();
}

/// Displays the character to the LEDs using the given color
/// bool *character: Boolean array of length 7 representing the 7 segment of 1 digit
/// CRGB *leds: LEDs RGBW (not RGB) array
/// CRGB color: The color for the lit segments
/// int length: The length of the LED strip
void displayCharacterToLeds(bool *character, CRGBW *leds, CRGB onColor, int length) {
  for (int i = 0; i < length; i++) {
    leds[i] = character[i] ? onColor : CRGB::Black;
  }
}
