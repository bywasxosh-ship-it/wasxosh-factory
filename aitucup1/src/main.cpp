#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "net.h"
#include "audio.h"
#include "ai_client.h"
#include "lcd_ui.h"

enum class Lang { RU, EN, KK };
static Lang srcLang = Lang::RU;
static Lang dstLang = Lang::EN;

static unsigned long tPressStart = 0;
static bool tWasPressed = false;

static unsigned long lastBtn1 = 0, lastBtn2 = 0;
static const unsigned long DEBOUNCE_MS = 200;

static const char* langCode(Lang l) {
  switch (l) {
    case Lang::RU: return "ru";
    case Lang::EN: return "en";
    case Lang::KK: return "kk";
  }
  return "ru";
}

static String langPairStr() {
  return String(langCode(srcLang)) + "->" + String(langCode(dstLang));
}

static void swapLangs() {
  Lang t = srcLang; srcLang = dstLang; dstLang = t;
}

static void cycleTarget() {
  // меняем dst по кругу, но не равный src
  Lang next = dstLang;
  for (int i=0;i<3;i++) {
    if (next == Lang::RU) next = Lang::EN;
    else if (next == Lang::EN) next = Lang::KK;
    else next = Lang::RU;
    if (next != srcLang) break;
  }
  dstLang = next;
}

static void fullCycle(unsigned long heldMs) {
  unsigned long maxMs = (unsigned long)MAX_RECORD_SEC * 1000UL;
  if (heldMs > maxMs) heldMs = maxMs;
  if (heldMs < 250) return;

  size_t samples = (size_t)MIC_SAMPLE_RATE * (size_t)heldMs / 1000;
  int16_t* pcm = (int16_t*)malloc(samples * sizeof(int16_t));
  if (!pcm) { lcdSet(LcdStatus::Error, "malloc", "fail"); return; }

  lcdSet(LcdStatus::Listening, langPairStr(), "Speak...");
  audioInitMic();
  delay(30);

  if (!micRecordPcm16(pcm, samples)) {
    free(pcm);
    lcdSet(LcdStatus::Error, "mic", "read fail");
    return;
  }

  lcdSet(LcdStatus::Processing, "STT...", langPairStr());
  String heard = doSttRawPcmB64(pcm, samples, langCode(srcLang));
  free(pcm);

  if (!heard.length()) {
    lcdSet(LcdStatus::Error, "empty stt", "");
    delay(700);
    lcdSet(LcdStatus::Idle, "Hold to talk", langPairStr());
    return;
  }

  lcdSet(LcdStatus::Processing, "CHAT...", heard.substring(0, LCD_COLS));
  String ans = doChat(heard, langCode(srcLang), langCode(dstLang));

  if (!ans.length()) {
    lcdSet(LcdStatus::Error, "empty chat", "");
    delay(700);
    lcdSet(LcdStatus::Idle, "Hold to talk", langPairStr());
    return;
  }

  lcdSet(LcdStatus::Processing, "TTS...", ans.substring(0, LCD_COLS));
  uint8_t* wav = nullptr;
  size_t wavLen = 0;
  if (!doTtsWav(ans, &wav, &wavLen)) {
    lcdSet(LcdStatus::Error, "tts", "fail");
    delay(700);
    lcdSet(LcdStatus::Idle, "Hold to talk", langPairStr());
    return;
  }

  lcdSet(LcdStatus::Playing, "Playing...", langPairStr());
  speakerPlayWav(wav, wavLen);
  free(wav);

  lcdSet(LcdStatus::Idle, "Hold to talk", langPairStr());
  audioInitMic();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_PTT, INPUT_PULLUP);
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);

  lcdInit();
  lcdSet(LcdStatus::Idle, "S1 start", "");

  lcdSet(LcdStatus::Idle, "WiFi...", "");
  if (!ensureWiFi()) { lcdSet(LcdStatus::Error, "wifi", "fail"); return; }

  int code=0; String body;
  lcdSet(LcdStatus::Idle, "/health...", "");
  if (!httpGet("/health", code, body, 5000)) {
    lcdSet(LcdStatus::Error, "health", String("code ") + String(code));
    Serial.printf("/health FAIL code=%d body=%s\n", code, body.c_str());
    return;
  }

  audioInitMic();

  lcdSet(LcdStatus::Idle, "Hold to talk", langPairStr());
  Serial.println("READY");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) ensureWiFi();

  unsigned long now = millis();

  // BTN1 swap
  if (digitalRead(PIN_BTN1) == LOW && (now - lastBtn1) > DEBOUNCE_MS) {
    lastBtn1 = now;
    swapLangs();
    lcdSet(LcdStatus::Idle, "Swap", langPairStr());
    delay(150);
    lcdSet(LcdStatus::Idle, "Hold to talk", langPairStr());
  }

  // BTN2 cycle target
  if (digitalRead(PIN_BTN2) == LOW && (now - lastBtn2) > DEBOUNCE_MS) {
    lastBtn2 = now;
    cycleTarget();
    lcdSet(LcdStatus::Idle, "Lang", langPairStr());
    delay(150);
    lcdSet(LcdStatus::Idle, "Hold to talk", langPairStr());
  }

  // PTT hold-to-talk
  bool pressed = (digitalRead(PIN_PTT) == LOW);
  if (pressed && !tWasPressed) {
    tWasPressed = true;
    tPressStart = now;
  }
  if (!pressed && tWasPressed) {
    tWasPressed = false;
    fullCycle(now - tPressStart);
  }

  delay(5);
}
