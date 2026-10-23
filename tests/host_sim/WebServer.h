// Stand-in for <WebServer.h> (signatures from esp32 core 3.0.1 libraries/WebServer/src/WebServer.h).
// send()/send_P() record what the handler produced so the simulator can hand it to the tests.
#pragma once
#include "arduino_shim.h"
extern std::string lastBody, lastType;
extern int lastCode;
extern const char* lastPgm;
class WebServer {
 public:
  WebServer(int port = 80) { (void)port; }
  void begin() {}
  void handleClient() {}
  void on(const char* uri, void (*fn)()) { (void)uri; (void)fn; }
  void sendHeader(const String&, const String&, bool = false) {}
  void send(int code, const char* type = NULL, const String& content = String("")) {
    lastCode = code; lastType = type ? type : ""; lastBody = content.c_str();
  }
  void send_P(int code, PGM_P type, PGM_P content) { lastCode = code; lastType = type; lastPgm = content; }
};
