// Stand-in for <WiFi.h> (signatures from esp32 core 3.0.1 libraries/WiFi/src/WiFiAP.h)
#pragma once
#include "arduino_shim.h"
#define WIFI_AP 2
struct WiFiClass {
  void mode(int) {}
  bool softAP(const char*, const char* = NULL, int = 1, int = 0, int = 4, bool = false) { return true; }
  IPAddress softAPIP() { return IPAddress(); }
};
extern WiFiClass WiFi;
