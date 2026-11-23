#pragma once
#include "MockArduino.h"
#include <utility>
template<int,int> struct Board_XIAO_1inch14_LCD {};
struct Config_Seeed_1inch14_LCD_ST7789 {};
struct Seeed_GFX {
  template<class A,class B> bool begin() {return true;}
  void setRotation(int) {}
};
struct Seeed_Sprite {
  int cursorY=0;
  std::vector<std::pair<int,std::string>> texts;
  void setColorDepth(int) {}
  bool createSprite(Seeed_GFX&,int,int) {return true;}
  void setTextSize(int) {}
  void setTextColor(uint16_t) {}
  void setTextFont(int) {}
  void setTextWrap(bool) {}
  void setCursor(int,int y) {cursorY=y;}
  void print(const char* s) {texts.emplace_back(cursorY,s);}
  void fillScreen(uint16_t) {texts.clear();}
  void fillRoundRect(int,int,int,int,int,uint16_t) {}
  void fillTriangle(int,int,int,int,int,int,uint16_t) {}
  void fillCircle(int,int,int,uint16_t) {}
  void fillRect(int,int,int,int,uint16_t) {}
  void drawLine(int,int,int,int,uint16_t) {}
  void pushSprite(int,int) {}
  std::string lastTextAt(int y) {
    for(auto i=texts.rbegin();i!=texts.rend();++i) if(i->first==y) return i->second;
    return {};
  }
};
