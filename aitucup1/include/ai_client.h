#pragma once
#include <Arduino.h>

String doSttRawPcmB64(const int16_t* pcm, size_t samples, const char* langHint="ru");
String doChat(const String& text, const char* userLang="ru", const char* assistantLang="ru");
bool doTtsWav(const String& text, uint8_t** wavOut, size_t* lenOut, const char* voice="marin");
