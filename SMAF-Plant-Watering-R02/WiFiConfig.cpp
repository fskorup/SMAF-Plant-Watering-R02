#include "WiFiConfig.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences prefs;

void handleWebSocketMessage(AsyncWebSocketClient *client, String data) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data);
  if (err) return;

  String action = doc["action"];

  if (action == "get_config") {
    JsonDocument response;
    response["action"] = "config_data";

    prefs.begin("wifi_config", true);
    response["ssidName"] = prefs.getString("ssidName", "");
    response["ssidPassword"] = prefs.getString("ssidPassword", "");
    response["mqttServer"] = prefs.getString("mqttServer", "");
    response["mqttServerPort"] = prefs.getInt("mqttServerPort", 1883);
    response["mqttUsername"] = prefs.getString("mqttUsername", "");
    response["mqttPassword"] = prefs.getString("mqttPassword", "");
    response["mqttClientId"] = prefs.getString("mqttClientId", "");
    response["mqttTopic"] = prefs.getString("mqttTopic", "");
    response["rgb"] = prefs.getBool("rgb", true);
    response["buzzer"] = prefs.getBool("buzzer", true);
    prefs.end();

    String json;
    serializeJson(response, json);
    client->text(json);
  }

  else if (action == "save_config") {
    prefs.begin("wifi_config", false);

    prefs.putString("ssidName", doc["ssidName"] | "");
    prefs.putString("ssidPassword", doc["ssidPassword"] | "");
    prefs.putString("mqttServer", doc["mqttServer"] | "");
    prefs.putInt("mqttServerPort", doc["mqttServerPort"] | 1883);
    prefs.putString("mqttUsername", doc["mqttUsername"] | "");
    prefs.putString("mqttPassword", doc["mqttPassword"] | "");
    prefs.putString("mqttClientId", doc["mqttClientId"] | "");
    prefs.putString("mqttTopic", doc["mqttTopic"] | "");
    prefs.putBool("rgb", doc["rgb"]);
    prefs.putBool("buzzer", doc["buzzer"]);
    prefs.end();

    client->text("{\"action\":\"save_ack\",\"status\":\"ok\"}");
    delay(1000);
    ESP.restart();
  }

  else if (action == "scan_wifi") {
    int n = WiFi.scanNetworks();
    JsonDocument response;
    response["action"] = "wifi_list";

    JsonArray ssids = response["ssids"].to<JsonArray>();
    for (int i = 0; i < n; ++i) {
      ssids.add(WiFi.SSID(i));
    }

    String json;
    serializeJson(response, json);
    client->text(json);
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    String msg;
    for (size_t i = 0; i < len; i++) msg += (char)data[i];
    handleWebSocketMessage(client, msg);
  }
}

void setupWiFiConfig() {
  WiFi.softAP("SMAD-DK-SAP-Configuration", "0123456789");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }

//   Serial.println("Files in LittleFS:");
//   File root = LittleFS.open("/");
//   while (File file = root.openNextFile()) {
//     Serial.println(file.name());
//   }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve the HTML page.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", String(), false);
  });

  // Serve static files for web page.
  server.serveStatic("/style.css", LittleFS, "/style.css");
  server.serveStatic("/script.js", LittleFS, "/script.js");

  server.begin();

  // Serial.println("Web server started at: http://192.168.4.1");
}

void clearWiFiConfig() {
    Preferences prefs;
    prefs.begin("wifi_config", false);  // false = write mode
    prefs.clear();                      // wipe all keys in this namespace
    prefs.end();
}

WiFiConfig loadWiFiConfig() {
    Preferences prefs;
    WiFiConfig config;

    prefs.begin("wifi_config", true); // read-only
    config.ssidName = prefs.getString("ssidName", "");
    config.ssidPassword = prefs.getString("ssidPassword", "");
    config.mqttServer = prefs.getString("mqttServer", "");
    config.mqttServerPort = prefs.getInt("mqttServerPort", 1883);
    config.mqttUsername = prefs.getString("mqttUsername", "");
    config.mqttPassword = prefs.getString("mqttPassword", "");
    config.mqttClientId = prefs.getString("mqttClientId", "");
    config.mqttTopic = prefs.getString("mqttTopic", "");
    config.rgb = prefs.getBool("rgb", true);
    config.buzzer = prefs.getBool("buzzer", true);
    prefs.end();

    return config;
}