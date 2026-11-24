#include "net.h"

#include <WiFi.h>
#include <LEAmDNS.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>

#include "../shared/protocol.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "firmware/pico/secrets.h not found: using secrets.example.h (device will fall back to its own AP)"
#include "secrets.example.h"
#endif

namespace net {
namespace {

constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t STA_RETRY_MS           = 30000;

WebServer        http(centad::HTTP_PORT);
WebSocketsServer ws(centad::WS_PORT);

Mode           mode_ = Mode::AccessPoint;
bool           fs_ok_ = false;
TextHandler    on_text_ = nullptr;
ConnectHandler on_connect_ = nullptr;
uint32_t       sta_lost_at_ = 0;

const char* contentType(const String& p) {
  if (p.endsWith(".html")) return "text/html";
  if (p.endsWith(".js"))   return "application/javascript";
  if (p.endsWith(".css"))  return "text/css";
  if (p.endsWith(".json") || p.endsWith(".webmanifest")) return "application/json";
  if (p.endsWith(".png"))  return "image/png";
  if (p.endsWith(".svg"))  return "image/svg+xml";
  if (p.endsWith(".ico"))  return "image/x-icon";
  if (p.endsWith(".woff2")) return "font/woff2";
  return "application/octet-stream";
}

// Static files from LittleFS (data/ folder). "/" -> /index.html (reward display),
// "/panel/" -> /panel/index.html (clinician panel). Serves foo.gz when foo is absent.
void handleStatic() {
  String path = http.uri();
  // Bare "/" -> the reward display with its WebSocket bridge pointed back at us.
  if (path == "/" && http.args() == 0) { http.sendHeader("Location", "/?ws=auto"); http.send(302); return; }
  if (path.endsWith("/")) path += "index.html";
  if (path == "/panel") { http.sendHeader("Location", "/panel/"); http.send(302); return; }

  if (!fs_ok_) {
    http.send(503, "text/plain",
              "LittleFS not mounted. Run scripts/build_pico_data.sh then `pio run -e pico -t uploadfs`.");
    return;
  }
  String fsPath = path;
  if (!LittleFS.exists(fsPath)) {
    fsPath += ".gz";
    if (!LittleFS.exists(fsPath)) {
      http.send(404, "text/plain", "Not found: " + path);
      return;
    }
  }
  File f = LittleFS.open(fsPath, "r");
  // index.html / sw.js must revalidate so updates show up; hashed assets can cache.
  const bool hashed = path.startsWith("/panel/assets/");
  http.sendHeader("Cache-Control", hashed ? "max-age=31536000, immutable" : "no-cache");
  http.streamFile(f, contentType(path));
  f.close();
}

void wsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[ws] client %u connected (%u total)\n", num, ws.connectedClients());
      if (on_connect_) on_connect_(num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("[ws] client %u disconnected\n", num);
      break;
    case WStype_TEXT:
      if (on_text_) on_text_(num, (const char*)payload, length);
      break;
    default:
      break;
  }
}

bool connectStation() {
  if (strcmp(WIFI_SSID, "your-network") == 0) {
    Serial.println("[wifi] no secrets.h credentials, skipping station mode");
    return false;
  }
  Serial.printf("[wifi] joining \"%s\" ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(centad::MDNS_HOST);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < STA_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void startAccessPoint() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(centad::AP_SSID, centad::AP_PASSWORD);
  mode_ = Mode::AccessPoint;
  Serial.printf("[wifi] AP \"%s\" (password \"%s\") at %s\n", centad::AP_SSID, centad::AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());
}

}  // namespace

void begin(TextHandler onText, ConnectHandler onConnect) {
  on_text_ = onText;
  on_connect_ = onConnect;

  fs_ok_ = LittleFS.begin();
  Serial.printf("[fs] LittleFS %s\n", fs_ok_ ? "mounted" : "NOT mounted (web files missing)");

  if (connectStation()) {
    mode_ = Mode::Station;
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[wifi] station failed, starting fallback access point");
    startAccessPoint();
  }

  if (MDNS.begin(centad::MDNS_HOST)) {
    MDNS.addService("http", "tcp", centad::HTTP_PORT);
    MDNS.addService("ws", "tcp", centad::WS_PORT);
    Serial.printf("[mdns] http://%s.local/\n", centad::MDNS_HOST);
  } else {
    Serial.println("[mdns] failed to start");
  }

  http.onNotFound(handleStatic);
  http.begin();

  ws.begin();
  ws.onEvent(wsEvent);
  ws.enableHeartbeat(5000, 3000, 2);   // drop dead clients (iPad sleeping etc.)

  Serial.printf("[ws] ws://%s:%u  (or ws://%s.local:%u)\n", ip().toString().c_str(), centad::WS_PORT,
                centad::MDNS_HOST, centad::WS_PORT);
}

void loop() {
  ws.loop();
  http.handleClient();
  MDNS.update();

  // Station mode: if the router drops us, keep retrying (the core reconnects
  // on its own most of the time; this is the belt-and-braces path).
  if (mode_ == Mode::Station) {
    if (WiFi.status() == WL_CONNECTED) {
      sta_lost_at_ = 0;
    } else if (sta_lost_at_ == 0) {
      sta_lost_at_ = millis();
      Serial.println("[wifi] lost connection");
    } else if (millis() - sta_lost_at_ > STA_RETRY_MS) {
      sta_lost_at_ = millis();
      Serial.println("[wifi] retrying station connection");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
}

void broadcast(const char* text, size_t len) { ws.broadcastTXT(text, len); }
void sendTo(uint8_t client, const char* text, size_t len) { ws.sendTXT(client, text, len); }
uint8_t clientCount() { return ws.connectedClients(); }
Mode mode() { return mode_; }
IPAddress ip() { return mode_ == Mode::Station ? WiFi.localIP() : WiFi.softAPIP(); }
bool fsMounted() { return fs_ok_; }

}  // namespace net
