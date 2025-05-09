/**
* @file SMAF-Plant-Watering.ino
* @brief Main Arduino sketch for the SMAF-Plant-Watering project.
*
* @license MIT License
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

#include "WiFi.h"
#include "PubSubClient.h"
#include "AudioVisualNotifications.h"
#include "WiFiConfig.h"
#include "Helpers.h"
#include "ArduinoJson.h"
#include "time.h"

// Define constants for ESP32 core numbers.
#define ESP32_CORE_PRIMARY 0    // Numeric value representing the primary core.
#define ESP32_CORE_SECONDARY 1  // Numeric value representing the secondary core.

// Enum to represent different device statuses.
enum DeviceStatusEnum : byte {
  NONE,              // Disable RGB led.
  NOT_READY,         // Device is not ready.
  READY_TO_SEND,     // Device is ready to send data.
  MAINTENANCE_MODE,  // Device is in maintenance mode.
  WATERING_MODE      // Device is in watering mode.
};

// Variable to store the current device status.
DeviceStatusEnum deviceStatus = NONE;  // Initial state is set to NOT_READY.

// Function prototype for the DeviceStatusThread function.
void DeviceStatusThread(void* pvParameters);

// Preferences variables.
String networkName = String();
String networkPass = String();
String mqttServerAddress = String();
String mqttUsername = String();
String mqttPass = String();
String mqttClientId = String();
String mqttPingTopic = String();
String mqttCommandTopic = String();
uint16_t mqttServerPort = 0;
bool visualNotifications = false;
bool audioNotifications = false;
bool isWatering = false;

/**
* @brief WiFiClient and PubSubClient instances for establishing MQTT communication.
* 
* The WiFiClient instance, named wifiClient, is used to manage the Wi-Fi connection.
* The PubSubClient instance, named mqtt, relies on the WiFiClient for MQTT communication.
*/
WiFiClient wifiClient;          // Manages Wi-Fi connection.
PubSubClient mqtt(wifiClient);  // Uses WiFiClient for MQTT communication.

/**
* @brief Constructs an instance of the AudioVisualNotifications class.
*
* Initializes an instance of the AudioVisualNotifications class with the provided configurations.
* The NeoPixel pin should be set up as OUTPUT before calling this constructor.
*
* @param neoPixelPin The pin connected to the NeoPixel LED strip.
* @param neoPixelCount The number of NeoPixels in the LED strip.
* @param neoPixelBrightness The brightness level of the NeoPixels (0-255).
* @param speakerPin The pin connected to the speaker for audio feedback.
*/
AudioVisualNotifications notifications(4, 2, 30, 5);

// Define the pin for the configurationuration button.
int configurationButton = 6;
int solenoidPin = 8;

// NTP Server configuration.
const char* ntpServer = "europe.pool.ntp.org";  // Global - pool.ntp.org
const long gmtOffset = 0;
const int dstOffset = 0;

/**
* @brief Initializes the SMAF-Development-Kit and runs once at the beginning.
*
* This function is responsible for the initial setup of the SMAF-Development-Kit.
* It is executed once when the Arduino board starts or is reset.
*
*/
void setup() {
  // Create a new task (DeviceStatusThread) and assign it to the primary core (ESP32_CORE_PRIMARY).
  xTaskCreatePinnedToCore(
    DeviceStatusThread,    // Function to implement the task.
    "DeviceStatusThread",  // Name of the task.
    8000,                  // Stack size in words.
    NULL,                  // Task input parameter (e.g., delay).
    1,                     // Priority of the task.
    NULL,                  // Task handle.
    ESP32_CORE_SECONDARY   // Core where the task should run.
  );

  // Initialize serial communication at a baud rate of 115200.
  Serial.begin(115200);

  // Set the pin mode for the configuration button to INPUT.
  pinMode(configurationButton, INPUT);
  pinMode(solenoidPin, OUTPUT);
  digitalWrite(solenoidPin, LOW);

  // Delay for 2400 milliseconds (2.4 seconds).
  delay(1600);

  // Print a formatted welcome message with build information.
  String buildVersion = "v0.002";
  String buildDate = "Q2, 2025.";
  Serial.printf("\n\rSMAF-PLANT-WATERING-KIT, Crafted with love in Europe.\n\rBuild version: %s\n\rBuild date: %s\n\r\n\r", buildVersion, buildDate);

  // Initialize NTP server time configuration.
  configTime(gmtOffset, dstOffset, ntpServer);

  // MQTT Client message buffer size.
  // Default is set to 256.
  mqtt.setBufferSize(1024);

  // Load and check configuration.
  WiFiConfig config = loadWiFiConfig();

  debug(SCS, "Loaded WiFi/MQTT Configuration");
  debug(LOG, "SSID Name: %s", config.ssidName.c_str());
  debug(LOG, "SSID Password: %s", config.ssidPassword.c_str());
  debug(LOG, "MQTT Server: %s", config.mqttServer.c_str());
  debug(LOG, "MQTT Port: %s", String(config.mqttServerPort));
  debug(LOG, "MQTT Username: %s", config.mqttUsername.c_str());
  debug(LOG, "MQTT Password: %s", config.mqttPassword.c_str());
  debug(LOG, "MQTT Client ID: %s", config.mqttClientId.c_str());
  debug(LOG, "MQTT Topic: %s", config.mqttTopic.c_str());
  debug(LOG, "RGB Enabled: %s", String(config.rgb ? "true" : "false"));
  debug(LOG, "Buzzer Enabled: %s", String(config.buzzer ? "true" : "false"));

  static String mqttPingTopicStr = config.mqttTopic + "/status";
  static String mqttCommandTopicStr = config.mqttTopic + "/cmd";

  networkName = config.ssidName;
  networkPass = config.ssidPassword;
  mqttServerAddress = config.mqttServer;
  mqttUsername = config.mqttUsername;
  mqttPass = config.mqttPassword;
  mqttClientId = config.mqttClientId;
  mqttServerPort = config.mqttServerPort;
  mqttPingTopic = mqttPingTopicStr;
  mqttCommandTopic = mqttCommandTopicStr;
  visualNotifications = config.rgb ? true : false;
  audioNotifications = config.buzzer ? true : false;

  // Initialize visualization library neo pixels.
  // This does not light up neo pixels.
  notifications.visual.initializePixels();

  // Play intro melody on speaker if enabled in preferences.
  if (audioNotifications) {
    notifications.audio.introMelody();
  }

  ua3qddd7lc

  delay(1200);

  static bool isConfigurationValid = config.ssidName.length() > 0 && config.mqttServer.length() > 0 && config.mqttClientId.length() > 0 && config.mqttTopic.length() > 0 && config.mqttServerPort > 0;

  if (isConfigurationValid) {
    debug(SCS, "Configuration is valid. All required configuration data is present.");
  } else {
    debug(ERR, "Configuration is incomplete. Some required fields are missing.");
  }

  if ((digitalRead(configurationButton) == LOW) || (!isConfigurationValid)) {
    debug(CMD, "Starting WiFi configuration.");

    // Set device status to Maintenance Mode.
    deviceStatus = MAINTENANCE_MODE;

    // Start configuration server.
    setupWiFiConfig();

    // Play maintenance melody on speaker even if disabled in preferences.
    notifications.audio.maintenanceMelody();

    // Block here until config is done and ESP restarts
    while (true) {
      delay(80);
      yield();  // Prevent watchdog reset.
    }
  }

  // Setup hardware Watchdog timer. Bark Bark.
  initWatchdog(30, true);
}

/**
* @brief Main execution loop for the SMAF-Development-Kit.
*
* This function runs repeatedly in a loop after the initial setup.
* It is the core of your Arduino program, where continuous tasks and operations should be placed.
* Be mindful of keeping the loop efficient and avoiding long blocking operations.
*
*/
void loop() {
  static unsigned long mqttPostTimer = 0;

  if (millis() - mqttPostTimer >= 2000) {
    mqttPostTimer = millis();

    // Attempt to connect to the Wi-Fi network.
    connectToNetwork();

    // Attempt to connect to the MQTT broker.
    connectToMqttBroker();

    // Store MQTT data here.
    String mqttData = String();
    String timestamp = getUtcTimeString();
    mqttData = constructMqttMessage(timestamp, isWatering);

    // Publish a message to the MQTT broker.
    debug(CMD, "Posting data package to MQTT broker '%s' on topic '%s'.", mqttServerAddress.c_str(), mqttPingTopic.c_str());
    mqtt.publish(mqttPingTopic.c_str(), mqttData.c_str(), false);
  }

  // Check for incoming data on defined MQTT topic.
  // This is hard core connection check.
  // If no data on topic is received, we are not connected to internet or server and watchdog will reset the device.
  mqtt.loop();
}

/**
* @brief Handles the server response received on a specific MQTT topic.
*
* This function logs the server response using debug output. If the device status is not
* in maintenance mode, it also resets the watchdog timer to prevent system reset.
*
* @param topic The MQTT topic on which the server response was received.
* @param payload Pointer to the payload data received from the server.
* @param length Length of the payload data.
*/
void serverResponse(char* topic, byte* payload, unsigned int length) {
  debug(SCS, "Server '%s' responded. Message received on topic: '%s'", mqttServerAddress.c_str(), topic);

  // Optional: compare topic strings if you need to react differently
  if (strcmp(topic, mqttPingTopic.c_str()) == 0) {
    // Convert payload to string
    char messageBuffer[length + 1];
    memcpy(messageBuffer, payload, length);
    messageBuffer[length] = '\0';  // Null-terminate

    debug(SCS, "Payload: %s", messageBuffer);

    // Reset WDT.
    if (deviceStatus != MAINTENANCE_MODE) {
      resetWatchdog();
    }
  } else if (strcmp(topic, mqttCommandTopic.c_str()) == 0) {
    // Convert payload to string
    char messageBuffer[length + 1];
    memcpy(messageBuffer, payload, length);
    messageBuffer[length] = '\0';  // Null-terminate

    debug(SCS, "Payload: %s", messageBuffer);

    // Parse JSON
    // StaticJsonDocument<200> doc;  // Adjust size as needed
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, messageBuffer);

    if (error) {
      debug(ERR, "Failed to parse JSON: %s", error.c_str());
      return;
    }

    // Access values
    isWatering = doc["watering"];

    if (isWatering) {
      debug(SCS, "Watering plants in progress");
      deviceStatus = WATERING_MODE;
      digitalWrite(solenoidPin, HIGH);
    } else {
      debug(SCS, "Watering plants complete");
      deviceStatus = READY_TO_SEND;
      digitalWrite(solenoidPin, LOW);
    }
  }
}

/**
* @brief Attempt to connect SMAF-DK to the configurationured Wi-Fi network.
*
* If SMAF-DK is not connected to the Wi-Fi network, this function tries to establish
* a connection using the settings from the WiFiconfiguration instance.
*
* @warning This function may delay for extended periods while attempting to connect
* to the Wi-Fi network.
*/
void connectToNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    // Set initial device status.
    deviceStatus = NOT_READY;

    // Disable auto-reconnect and set Wi-Fi mode to station mode.
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);

    // Log an error if not connected to the configurationured SSID.
    debug(ERR, "Device not connected to '%s'.", networkName.c_str());

    // Keep attempting to connect until successful.
    while (WiFi.status() != WL_CONNECTED) {
      debug(CMD, "Connecting device to '%s'", networkName.c_str());

      // Attempt to connect to the Wi-Fi network using configurationured credentials.
      WiFi.begin(networkName, networkPass);
      delay(6400);
    }

    // Log successful connection and set device status.
    debug(SCS, "Device connected to '%s'.", networkName.c_str());
  }
}

/**
* @brief Attempt to connect to the configurationured MQTT broker.
*
* If the MQTT client is not connected, this function tries to establish a connection
* to the MQTT broker using the settings from the WiFiconfiguration instance.
*
* @note Assumes that MQTT configurationuration parameters (server address, port, client ID,
* username, password) have been previously set in the WiFiconfiguration instance.
*
* @warning This function may delay for extended periods while attempting to connect
* to the MQTT broker.
*/
void connectToMqttBroker() {
  if (!mqtt.connected()) {
    // Set initial device status.
    deviceStatus = NOT_READY;

    // Set MQTT server and connection parameters.
    mqtt.setServer(mqttServerAddress.c_str(), mqttServerPort);
    // mqtt.setKeepAlive(30000);     // To be configurationured on the settings page.
    // mqtt.setSocketTimeout(4000);  // To be configurationured on the settings page.
    mqtt.setCallback(serverResponse);

    // Log an error if not connected.
    debug(ERR, "Device not connected to MQTT broker '%s'.", mqttServerAddress.c_str());

    // Keep attempting to connect until successful.
    while (!mqtt.connected()) {
      debug(CMD, "Connecting device to MQTT broker '%s'.", mqttServerAddress.c_str());

      if (mqtt.connect(mqttClientId.c_str(), mqttUsername.c_str(), mqttPass.c_str())) {
        // Log successful connection and set device status.
        debug(SCS, "Device connected to MQTT broker '%s'.", mqttServerAddress.c_str());

        // Subscribe to MQTT topics.
        mqtt.subscribe(mqttPingTopic.c_str());
        mqtt.subscribe(mqttCommandTopic.c_str());

        deviceStatus = READY_TO_SEND;
      } else {
        // Retry after a delay if connection failed.
        delay(4000);
      }
    }
  }
}

/**
* @brief Retrieves the current UTC time as a formatted string.
*
* This function retrieves the current UTC time using the system time. If successful,
* it formats the time into a UTC date time string (e.g., "2024-06-20T20:56:59Z").
* If the UTC time cannot be obtained, it returns "Unknown".
*
* @return A String containing the current UTC time in the specified format, or "Unknown" if the time cannot be retrieved.
*/
String getUtcTimeString() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    return "Unknown";
  }

  // Create a buffer to hold the formatted time string
  char buffer[80];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
}

/**
* @brief Constructs an MQTT message string containing GPS and time-related data.
*
* Constructs a JSON-formatted MQTT message string containing various GPS-related data
* (satellites in range, longitude, latitude, speed, heading, altitude) and time-related
* information (timestamp, GMT offset, DST offset).
*
* @param timestamp Human-readable timestamp in UTC format.
* @param satellitesInRange Number of satellites currently in range.
* @param longitude Longitude value in microdegrees (degrees * 1E-7).
* @param latitude Latitude value in microdegrees (degrees * 1E-7).
* @param altitude Altitude value in meters.
* @param speed Speed value in meters per second.
* @param heading Heading direction in microdegrees (degrees * 1E-5).
* @return A String containing the constructed MQTT message in JSON format.
*/
/*
String constructMqttMessage(uint8_t satellitesInRange, int32_t longitude, int32_t latitude, int32_t altitude, int32_t speed, int32_t heading, String timestamp) {
  String message;

  message += "{";
  message += quotation("timestamp") + ":" + quotation(timestamp) + ",";
  message += quotation("satellites") + ":" + String(satellitesInRange) + ",";
  message += quotation("longitude") + ":";
  message += "{";
  message += quotation("value") + ":" + String((longitude * 1E-7), 6) + ",";
  message += quotation("unit") + ":" + quotation("deg");
  message += "},";
  message += quotation("latitude") + ":";
  message += "{";
  message += quotation("value") + ":" + String((latitude * 1E-7), 6) + ",";
  message += quotation("unit") + ":" + quotation("deg");
  message += "},";
  message += quotation("altitude") + ":";
  message += "{";
  message += quotation("value") + ":" + String(int((altitude / 1000.0))) + ",";
  message += quotation("unit") + ":" + quotation("m");
  message += "},";
  message += quotation("speed") + ":";
  message += "{";
  message += quotation("value") + ":" + String(int((speed / 1000.0) * 3.6)) + ",";
  message += quotation("unit") + ":" + quotation("km/h");
  message += "},";
  message += quotation("heading") + ":";
  message += "{";
  message += quotation("value") + ":" + String((heading * 1E-5), 0) + ",";
  message += quotation("unit") + ":" + quotation("deg");
  message += "}";
  message += "}";

  return message;
}
*/

String constructMqttMessage(String timestamp, bool isWateringInProgress) {
  String message;

  message += "{";
  message += quotation("timestamp") + ":" + quotation(timestamp) + ",";
  message += quotation("watering") + ":" + (isWateringInProgress ? "true" : "false");
  message += "}";

  return message;
}

/**
* @brief Thread function for handling device status indications through an RGB LED.
*
* This thread continuously updates the RGB LED status based on the current device status.
* It uses the DeviceStatusEnum values to determine the appropriate LED indication.
*
* @param pvParameters Pointer to task parameters (not used in this function).
*/
void DeviceStatusThread(void* pvParameters) {
  while (true) {
    // Update LED status based on the current device status.
    if (visualNotifications) {
      // Clear the NeoPixel LED strip.
      notifications.visual.clearAllPixels();

      switch (deviceStatus) {
        case NONE:
          notifications.visual.notReadyMode();
          break;
        case NOT_READY:
          notifications.visual.notReadyMode();
          break;
        case READY_TO_SEND:
          notifications.visual.readyToSendMode();
          break;
        case WATERING_MODE:
          notifications.visual.waitingGnssFixMode();
          break;
        case MAINTENANCE_MODE:
          notifications.visual.maintenanceMode();
          break;
      }
    }

    // Add a delay to prevent WDT timeout.
    vTaskDelay(8 / portTICK_PERIOD_MS);  // Delay for 10 milliseconds.
  }
}