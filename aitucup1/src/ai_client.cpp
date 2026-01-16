#include "ai_client.h"
#include "net.h"
#include <ArduinoJson.h>

// ===== base64 =====
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static String base64Encode(const uint8_t* data, size_t len) {
  String out;
  out.reserve(((len + 2) / 3) * 4);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = 0;
    int pad = 0;
    n |= (uint32_t)data[i] << 16;
    if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8; else pad++;
    if (i + 2 < len) n |= (uint32_t)data[i + 2]; else pad++;

    out += b64_table[(n >> 18) & 63];
    out += b64_table[(n >> 12) & 63];
    out += (pad >= 2) ? '=' : b64_table[(n >> 6) & 63];
    out += (pad >= 1) ? '=' : b64_table[n & 63];
  }
  return out;
}

static String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') { /*skip*/ }
    else out += c;
  }
  return out;
}

static String extractBestText(const String& respJson) {
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, respJson)) return "";

  const char* a = doc["text"] | nullptr;
  if (a && *a) return String(a);

  const char* b = doc["output_text"] | nullptr;
  if (b && *b) return String(b);

  const char* c = doc["answer"] | nullptr;
  if (c && *c) return String(c);

  const char* d = doc["assistant_text"] | nullptr;
  if (d && *d) return String(d);

  return "";
}

String doSttRawPcmB64(const int16_t* pcm, size_t samples, const char* langHint) {
  size_t bytes = samples * sizeof(int16_t);
  String b64 = base64Encode((const uint8_t*)pcm, bytes);

  String json;
  json.reserve(b64.length() + 256);
  json += "{";
  json += "\"pcm_b64\":\""; json += b64; json += "\",";
  json += "\"sample_rate\":"; json += 16000; json += ",";
  json += "\"channels\":1,";
  json += "\"sample_width\":2,";
  json += "\"format\":\"pcm_s16le\",";
  json += "\"lang_hint\":\""; json += langHint; json += "\"";
  json += "}";

  int code = 0; String resp;
  if (!httpPostJson("/stt_raw", json, code, resp, 60000)) return "";
  return extractBestText(resp);
}

String doChat(const String& text, const char* userLang, const char* assistantLang) {
  String json;
  json.reserve(text.length() + 220);
  json += "{";
  json += "\"text\":\""; json += jsonEscape(text); json += "\",";
  json += "\"user_lang\":\""; json += userLang; json += "\",";
  json += "\"assistant_lang\":\""; json += assistantLang; json += "\"";
  json += "}";

  int code = 0; String resp;
  if (!httpPostJson("/chat", json, code, resp, 60000)) return "";
  return extractBestText(resp);
}

bool doTtsWav(const String& text, uint8_t** wavOut, size_t* lenOut, const char* voice) {
  String json;
  json.reserve(text.length() + 120);
  json += "{";
  json += "\"text\":\""; json += jsonEscape(text); json += "\",";
  json += "\"voice\":\""; json += voice; json += "\",";
  json += "\"format\":\"wav\"";
  json += "}";

  return httpPostJsonBinary("/tts", json, wavOut, lenOut, 60000);
}
