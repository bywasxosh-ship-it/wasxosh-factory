#pragma once
#include <Arduino.h>

void audioInitMic();
void audioInitSpeaker(int sampleRate);
bool micRecordPcm16(int16_t* out, size_t samples);
bool speakerPlayWav(const uint8_t* data, size_t len);
