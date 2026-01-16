#include "lcd_ui.h"
#include "config.h"
#include <LiquidCrystal.h>

static LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

static String fitN(const String& s, int n) {
  if ((int)s.length() >= n) return s.substring(0, n);
  String out = s;
  while ((int)out.length() < n) out += " ";
  return out;
}

static const char* stLabel(LcdStatus st) {
  switch (st) {
    case LcdStatus::Idle: return "IDLE";
    case LcdStatus::Listening: return "LISTEN";
    case LcdStatus::Processing: return "WORK";
    case LcdStatus::Playing: return "PLAY";
    case LcdStatus::Error: return "ERROR";
  }
  return "";
}

void lcdInit() {
  lcd.begin(LCD_COLS, LCD_ROWS);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Boot...");
}

void lcdSet(LcdStatus st, const String& line1, const String& line2) {
  String l1 = String(stLabel(st)) + " " + line1;
  String l2 = line2;
  lcd.setCursor(0,0); lcd.print(fitN(l1, LCD_COLS));
  if (LCD_ROWS > 1) { lcd.setCursor(0,1); lcd.print(fitN(l2, LCD_COLS)); }
}
