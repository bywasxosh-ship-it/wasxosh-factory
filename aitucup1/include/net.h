#pragma once
#include <Arduino.h>

bool ensureWiFi();
bool httpGet(const String& path, int& codeOut, String& bodyOut, uint32_t timeoutMs=5000);
bool httpPostJson(const String& path, const String& json, int& codeOut, String& bodyOut, uint32_t timeoutMs=20000);
bool httpPostJsonBinary(const String& path, const String& json, uint8_t** dataOut, size_t* lenOut, uint32_t timeoutMs=40000);
