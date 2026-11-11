// MonitorUp - siec: Ethernet W5500, WiFi klient, Access Point, mDNS
#include "net.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <ESPmDNS.h>
#include "hw.h"
#include "config.h"

bool ethStarted = false;
bool apOn = false;

bool ethHasIp() { return ethStarted && ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0); }
bool wifiHasIp() { return cfg.wifiEn && WiFi.status() == WL_CONNECTED; }

static IPAddress u2ip(uint32_t v) { return IPAddress(v); }

static void apStart() {
  if (apOn) return;
  WiFi.softAP(cfg.apSsid, cfg.apPass, cfg.apChannel);
  apOn = true;
  Serial.printf("AP: %s  IP %s\n", cfg.apSsid, WiFi.softAPIP().toString().c_str());
}

static void apStop() {
  if (!apOn) return;
  WiFi.softAPdisconnect(true);
  apOn = false;
  Serial.println("AP: wylaczony");
}

static void applyWifiMode() {
  bool wantAp = cfg.apMode == 0 || (cfg.apMode == 1 && !ethHasIp() && !wifiHasIp());
  wifi_mode_t m = cfg.wifiEn ? (wantAp ? WIFI_AP_STA : WIFI_STA) : (wantAp ? WIFI_AP : WIFI_OFF);
  WiFi.mode(m);
  if (wantAp) { apOn = false; apStart(); } else apOn = false;
}

void netBegin() {
  // Ethernet przed WiFi (przy odwrotnej kolejnosci zdarzal sie panic "stack canary ipc1")
  if (cfg.ethEn) {
    ethStarted = ETH.begin(ETH_PHY_W5500, 1, ETH_CS, ETH_INT, ETH_RST, SPI3_HOST, ETH_SCK, ETH_MISO, ETH_MOSI);
    if (ethStarted && !cfg.ethIp.dhcp)
      ETH.config(u2ip(cfg.ethIp.ip), u2ip(cfg.ethIp.gw), u2ip(cfg.ethIp.mask), u2ip(cfg.ethIp.dns1), u2ip(cfg.ethIp.dns2));
    Serial.printf("Ethernet W5500: %s\n", ethStarted ? "OK" : "BLAD");
  }
  delay(50);

  WiFi.persistent(false);
  WiFi.setHostname(MDNS_NAME);
  applyWifiMode();

  if (cfg.wifiEn && cfg.wifiSsid[0]) {
    if (!cfg.wifiIp.dhcp)
      WiFi.config(u2ip(cfg.wifiIp.ip), u2ip(cfg.wifiIp.gw), u2ip(cfg.wifiIp.mask), u2ip(cfg.wifiIp.dns1), u2ip(cfg.wifiIp.dns2));
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    Serial.printf("WiFi: lacze z %s\n", cfg.wifiSsid);
  }

  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
}

// Wywolywane co 1 s: log zmian IP, AP "tylko awaryjnie"
void netLoop() {
  static bool ethHad = false, wifiHad = false;
  static uint32_t noNetSince = 0;
  bool e = ethHasIp(), w = wifiHasIp();
  if (e && !ethHad) Serial.printf("Ethernet IP: %s (%u Mbps)\n", ETH.localIP().toString().c_str(), ETH.linkSpeed());
  if (w && !wifiHad) Serial.printf("WiFi IP: %s (RSSI %d)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  ethHad = e; wifiHad = w;

  if (cfg.apMode == 1) {
    if (e || w) { noNetSince = 0; if (apOn) apStop(); }
    else {
      if (!noNetSince) noNetSince = millis();
      if (!apOn && millis() - noNetSince > 60000) {
        WiFi.mode(cfg.wifiEn ? WIFI_AP_STA : WIFI_AP);
        apStart();
      }
    }
  }
}

