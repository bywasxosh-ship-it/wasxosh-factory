#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.printf("WiFi: connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - t > 20000) {
      Serial.println("\nWiFi: timeout");
      return false;
    }
  }

  WiFi.setSleep(false);
  Serial.println("\nWiFi OK");
  Serial.print("ESP IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

static bool beginHttp(HTTPClient& http, const String& path) {
  String url = String(SERVER_BASE) + path;
  return http.begin(url);
}

bool httpGet(const String& path, int& codeOut, String& bodyOut, uint32_t timeoutMs) {
  HTTPClient http;
  if (!beginHttp(http, path)) { codeOut = -1; bodyOut = ""; return false; }
  http.setTimeout(timeoutMs);

  int code = http.GET();
  String body = http.getString();
  http.end();

  codeOut = code;
  bodyOut = body;
  return (code == 200);
}

bool httpPostJson(const String& path, const String& json, int& codeOut, String& bodyOut, uint32_t timeoutMs) {
  HTTPClient http;
  if (!beginHttp(http, path)) { codeOut = -1; bodyOut = ""; return false; }
  http.setTimeout(timeoutMs);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST((uint8_t*)json.c_str(), json.length());
  String body = http.getString();
  http.end();

  codeOut = code;
  bodyOut = body;
  return (code == 200);
}

bool httpPostJsonBinary(const String& path, const String& json, uint8_t** dataOut, size_t* lenOut, uint32_t timeoutMs) {
  *dataOut = nullptr; *lenOut = 0;

  HTTPClient http;
  if (!beginHttp(http, path)) return false;
  http.setTimeout(timeoutMs);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST((uint8_t*)json.c_str(), json.length());
  if (code != 200) {
    http.end();
    return false;
  }

  int len = http.getSize();
  if (len <= 0) { http.end(); return false; }

  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf) { http.end(); return false; }

  WiFiClient* stream = http.getStreamPtr();
  size_t read = 0;
  while (http.connected() && read < (size_t)len) {
    size_t av = stream->available();
    if (av) {
      size_t chunk = stream->readBytes(buf + read, min(av, (size_t)len - read));
      read += chunk;
    }
    delay(1);
  }
  http.end();

  if (read != (size_t)len) { free(buf); return false; }

  *dataOut = buf;
  *lenOut = read;
  return true;
}
