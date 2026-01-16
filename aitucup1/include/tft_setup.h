#pragma once
#define USER_SETUP_LOADED

// Временно — если не знаешь экран, оставь заглушку.
// Позже поменяешь под свой дисплей.
#define ILI9341_DRIVER

#define TFT_MISO -1
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   13
#define TFT_RST  14

#define SPI_FREQUENCY  27000000
