#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <Arduino.h>

// Struct to hold the WiFi and MQTT configuration
struct WiFiConfig {
    String ssidName;
    String ssidPassword;
    String mqttServer;
    int mqttServerPort;
    String mqttUsername;
    String mqttPassword;
    String mqttClientId;
    String mqttTopic;
    bool rgb;
    bool buzzer;
};

// Initializes the configuration web server and WebSocket
void setupWiFiConfig();
void clearWiFiConfig();

// Loads the stored configuration into a struct
WiFiConfig loadWiFiConfig();

#endif // WIFI_CONFIG_H