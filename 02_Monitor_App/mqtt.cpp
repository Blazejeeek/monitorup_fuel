// Monitor App - MQTT + autodiscovery Home Assistant
#include "mqtt.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sensors.h"
#include "stats.h"
#include "outputs.h"
#include "net.h"
#include "logger.h"
#include "hw.h"
#include <esp_mac.h>

static WiFiClient mqttNet;
static PubSubClient mqtt(mqttNet);
static uint32_t mqttLastTry = 0, mqttLastPub = 0;
static bool mqttPubNow = false;
static uint8_t mqttLastEff[TANK_COUNT] = {255, 255};
static bool mqttLastAlarm = false, mqttLastAck = false;

bool mqttConnected() { return cfg.mqttEn && mqtt.connected(); }

static String devId() { uint8_t m[6]; esp_read_mac(m, ESP_MAC_WIFI_STA); char b[16]; snprintf(b, sizeof(b), "ma_%02x%02x%02x", m[3], m[4], m[5]); return b; }
static String topic(const String &s) { return String(cfg.mqttPrefix) + "/" + s; }

static void mqttPub(const String &t, const String &v, bool retain = true) { mqtt.publish(topic(t).c_str(), v.c_str(), retain); }
static String fnum(float v, int d) { return isnan(v) ? String("") : String(v, d); }

static void haEntity(const char *comp, const String &obj, const String &name, const String &stateT, const char *unit,
                     const char *devClass, const char *icon, const String &cmdT = "") {
  JsonDocument d;
  String uid = devId() + "_" + obj;
  d["name"] = name;
  d["uniq_id"] = uid;
  d["obj_id"] = uid;
  if (stateT.length()) d["stat_t"] = topic(stateT);
  if (cmdT.length()) d["cmd_t"] = topic(cmdT);
  d["avty_t"] = topic("status");
  if (unit) d["unit_of_meas"] = unit;
  if (devClass) d["dev_cla"] = devClass;
  if (icon) d["ic"] = icon;
  if (!strcmp(comp, "sensor") && unit) d["stat_cla"] = "measurement";
  if (!strcmp(comp, "binary_sensor")) { d["pl_on"] = "1"; d["pl_off"] = "0"; }
  JsonObject dev = d["dev"].to<JsonObject>();
  dev["ids"][0] = devId();
  dev["name"] = cfg.devName;
  dev["mf"] = "Monitor App";
  dev["mdl"] = "KinCony B8M";
  dev["sw"] = FW_VERSION;
  String out; serializeJson(d, out);
  mqtt.publish((String("homeassistant/") + comp + "/" + uid + "/config").c_str(), out.c_str(), true);
}

static void haDiscovery() {
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    if (!cfg.tank[i].enabled) continue;
    String z = "zbiornik" + String(i + 1), n = cfg.tankName[i];
    haEntity("sensor", z + "_cm", n + " poziom", z + "/poziom_cm", "cm", "distance", "mdi:waves-arrow-up");
    haEntity("sensor", z + "_proc", n + " wypełnienie", z + "/procent", "%", nullptr, "mdi:gauge");
    haEntity("sensor", z + "_ma", n + " prąd", z + "/mA", "mA", "current", nullptr);
    haEntity("sensor", z + "_stan", n + " stan", z + "/stan", nullptr, nullptr, "mdi:alert-circle-outline");
    if (cfg.tx[i].volEn) haEntity("sensor", z + "_l", n + " objętość", z + "/litry", "L", "volume_storage", nullptr);
    if (cfg.tx[i].statsEn) {
      const char *u = cfg.tx[i].volEn ? "L" : "cm";
      haEntity("sensor", z + "_zuz", n + " zużycie dziś", z + "/zuzycie_dzis", u, nullptr, "mdi:chart-bar");
      haEntity("sensor", z + "_zuz7", n + " zużycie średnie 7 dni", z + "/zuzycie_7d", u, nullptr, "mdi:chart-line");
      haEntity("sensor", z + "_prog", n + " wystarczy na", z + "/prognoza_dni", "d", nullptr, "mdi:calendar-clock");
    }
  }
  haEntity("binary_sensor", "alarm", "Alarm", "alarm_aktywny", nullptr, "problem", nullptr);
  if (cfg.mqttCmdEn) haEntity("button", "kasuj", "Kasuj alarm", "", nullptr, nullptr, "mdi:bell-cancel", "cmd/kasuj");
}

static void mqttPublishAll() {
  for (uint8_t i = 0; i < TANK_COUNT; i++) {
    if (!cfg.tank[i].enabled) continue;
    TankState s = tankGet(i);
    String z = "zbiornik" + String(i + 1) + "/";
    mqttPub(z + "poziom_cm", s.valid ? String(s.cm, 1) : "");
    mqttPub(z + "procent", s.valid ? String(s.pct, 1) : "");
    mqttPub(z + "mA", String(s.mA, 2));
    mqttPub(z + "stan", stateName(tankEffState(i)));
    if (cfg.tx[i].volEn) mqttPub(z + "litry", s.valid ? String(s.vol, 0) : "");
    if (cfg.tx[i].statsEn) {
      bool L = cfg.tx[i].volEn;
      mqttPub(z + "zuzycie_dzis", fnum(statConsToday(i, L), L ? 0 : 1));
      mqttPub(z + "zuzycie_7d", fnum(statAvg(i, 7, L), L ? 0 : 1));
      mqttPub(z + "prognoza_dni", fnum(statForecast(i), 1));
    }
  }
  JsonDocument d;
  d["aktywny"] = alarmActive; d["skwitowany"] = alarmAcked;
  String o; serializeJson(d, o);
  mqttPub("alarm", o);
  mqttPub("alarm_aktywny", alarmActive ? "1" : "0");
  JsonDocument w;
  for (uint8_t k = 0; k < 8; k++) w["out" + String(k + 1)] = outLevel[k];
  o = ""; serializeJson(w, o);
  mqttPub("wyjscia", o);
}

static void mqttCallback(char *t, byte *payload, unsigned int len) {
  if (!cfg.mqttCmdEn) return;
  String tp = t, pre = topic("cmd/");
  if (!tp.startsWith(pre)) return;
  String cmd = tp.substring(pre.length());
  String val; for (unsigned i = 0; i < len; i++) val += (char)payload[i];
  if (cmd == "kasuj") alarmAck(3);
  else if (cmd.startsWith("out/")) {
    int k = cmd.substring(4).toInt() - 1;
    if (k >= 0 && k < 8 && cfg.out[k].func == OF_MANUAL) cfg.out[k].manual = constrain(val.toInt(), 0, 100);
  }
}

void mqttApply() {
  if (mqtt.connected()) { mqttPub("status", "offline"); mqtt.disconnect(); }
  mqttLastTry = 0;
}

void mqttLoop() {
  if (!cfg.mqttEn || !cfg.mqttHost[0]) return;
  if (!ethHasIp() && !wifiHasIp()) return;
  if (!mqtt.connected()) {
    if (mqttLastTry && millis() - mqttLastTry < 15000) return;
    mqttLastTry = millis();
    mqtt.setServer(cfg.mqttHost, cfg.mqttPort);
    mqtt.setBufferSize(1024);
    mqtt.setCallback(mqttCallback);
    mqttNet.setTimeout(2);
    String lwt = topic("status");
    bool ok = mqtt.connect(devId().c_str(), cfg.mqttUser[0] ? cfg.mqttUser : nullptr, cfg.mqttUser[0] ? cfg.mqttPass : nullptr,
                           lwt.c_str(), 1, true, "offline");
    if (!ok) return;
    logEvent("MQTT", String("Połączono z ") + cfg.mqttHost);
    mqttPub("status", "online");
    if (cfg.mqttCmdEn) mqtt.subscribe(topic("cmd/#").c_str());
    if (cfg.mqttHa) haDiscovery();
    mqttPubNow = true;
  }
  mqtt.loop();
  for (uint8_t i = 0; i < TANK_COUNT; i++) { uint8_t e = tankEffState(i); if (e != mqttLastEff[i]) { mqttLastEff[i] = e; mqttPubNow = true; } }
  if (alarmActive != mqttLastAlarm || alarmAcked != mqttLastAck) { mqttLastAlarm = alarmActive; mqttLastAck = alarmAcked; mqttPubNow = true; }
  if (mqttPubNow || millis() - mqttLastPub >= (uint32_t)cfg.mqttIntervalS * 1000UL) {
    mqttPubNow = false;
    mqttLastPub = millis();
    mqttPublishAll();
  }
}
