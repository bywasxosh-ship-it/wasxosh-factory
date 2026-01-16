#pragma once
#include <Arduino.h>

enum class LcdStatus { Idle, Listening, Processing, Playing, Error };

void lcdInit();
void lcdSet(LcdStatus st, const String& line1, const String& line2="");
