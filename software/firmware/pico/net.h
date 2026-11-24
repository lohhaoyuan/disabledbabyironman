#pragma once
// Wi-Fi (STA with AP fallback) + mDNS + HTTP static files + WebSocket server.

#include <Arduino.h>

namespace net {

enum class Mode : uint8_t { Station, AccessPoint };

using TextHandler = void (*)(uint8_t client, const char* text, size_t len);
using ConnectHandler = void (*)(uint8_t client);

void begin(TextHandler onText, ConnectHandler onConnect);
void loop();                          // call often from loop()

void broadcast(const char* text, size_t len);
void sendTo(uint8_t client, const char* text, size_t len);
uint8_t clientCount();

Mode      mode();
IPAddress ip();
bool      fsMounted();

}  // namespace net
