#pragma once
#include <Arduino.h>

// ===== WiFi / Server =====
static const char* WIFI_SSID   = "216 5G";
static const char* WIFI_PASS   = "12344321";
static const char* SERVER_BASE = "http://192.168.1.49:8000"; // обязательно с http://

// ===== I2S (INMP441 + MAX98357A) =====
static const int PIN_WS   = 3;   // LRCLK / WS общий
static const int PIN_BCLK = 5;   // BCLK общий (если у тебя реально 12 -> поменяй тут и на микрофоне тоже)
static const int PIN_DIN  = 4;   // INMP441 SD
static const int PIN_DOUT = 18;  // ESP32 -> MAX98357A DIN

// ===== Buttons =====
// PTT (hold-to-talk)
static const int PIN_PTT  = 17;  // кнопку на GND, INPUT_PULLUP

// BTN1 = swap языков, BTN2 = cycle языков
static const int PIN_BTN1 = 14;  // кнопку на GND
static const int PIN_BTN2 = 7;  // кнопку на GND

// ===== Audio =====
static const int MIC_SAMPLE_RATE = 16000;
static const int MAX_RECORD_SEC  = 6;

// ===== LCD 1602 (HD44780) =====
static const int LCD_COLS = 16;
static const int LCD_ROWS = 2;

// 4-bit mode (RW на GND!)
static const int LCD_RS = 11;
static const int LCD_E  = 10;
static const int LCD_D4 = 9;
static const int LCD_D5 = 46;
static const int LCD_D6 = 16;
static const int LCD_D7 = 15;
