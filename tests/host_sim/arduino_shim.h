// Host-side stand-in for the Arduino / ESP32 API surface that smart_parking_v9 uses.
// Signatures mirror the installed ESP32 core (3.0.1): String +=, Serial.print overloads,
// WebServer, WiFi.softAP. The functions that need the OS or the sketch's own globals
// (millis, delay, pulseIn, digitalWrite, Serial input) are defined in host_sim.cpp.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <strings.h>

#define PROGMEM
typedef const char* PGM_P;
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0

unsigned long millis();
void delay(unsigned long ms);
inline void delayMicroseconds(unsigned int) {}
inline void pinMode(int, int) {}
void digitalWrite(int pin, int value);
long pulseIn(int pin, int level, unsigned long timeoutUs = 1000000UL);

class String {
  std::string s;
 public:
  String() {}
  String(const char* c) : s(c) {}
  String(float v, unsigned char dp = 2) { char b[40]; snprintf(b, sizeof b, "%.*f", (int)dp, v); s = b; }
  bool reserve(unsigned) { return true; }
  String& operator+=(const char* c) { s += c; return *this; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator+=(unsigned char v) { s += std::to_string((unsigned)v); return *this; }
  String& operator+=(int v) { s += std::to_string(v); return *this; }
  String& operator+=(unsigned int v) { s += std::to_string(v); return *this; }
  String& operator+=(long v) { s += std::to_string(v); return *this; }
  String& operator+=(unsigned long v) { s += std::to_string(v); return *this; }
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
};

struct IPAddress { const char* text = "192.168.4.1"; };

int serialAvailable();
int serialRead();

struct SerialShim {
  void begin(int) {}
  int available() { return serialAvailable(); }
  int read() { return serialRead(); }
  void print(const char* v) { fputs(v, stdout); }
  void print(char v) { putchar(v); }
  void print(unsigned char v) { printf("%u", (unsigned)v); }
  void print(int v) { printf("%d", v); }
  void print(unsigned int v) { printf("%u", v); }
  void print(long v) { printf("%ld", v); }
  void print(unsigned long v) { printf("%lu", v); }
  void print(double v, int dp = 2) { printf("%.*f", dp, v); }
  void print(const IPAddress& ip) { fputs(ip.text, stdout); }
  template <typename T> void println(T v) { print(v); putchar('\n'); }
  template <typename T> void println(T v, int dp) { print(v, dp); putchar('\n'); }
  void println() { putchar('\n'); }
};
extern SerialShim Serial;
