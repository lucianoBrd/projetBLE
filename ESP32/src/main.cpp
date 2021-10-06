#include <Arduino.h>

// For the screen
#include <TFT_eSPI.h>

// For bluetooth
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// For wifi and mqtt
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SPI.h>

#define BME_SCK 18
#define BME_MISO 19
#define BME_MOSI 23
#define BME_CS 5

void displayResult();

// Buttons
#define BUTTONGAUCHE 0  // btn de gauche

// Global var
const char *ssid = "B127-EIC";
const char *password = "b127-eic";
//const char *ssid = "Luciano";
//const char *password = "123456789";
const char *mqtt_server = "192.168.1.106";
const char *mqtt_topic_temp = "topic/temp";
const char *mqtt_topic_hum = "topic/hum";
const char *mqtt_topic_bat = "topic/bat";
const int mqtt_port = 1883;

int cptBtnG = 0; // compteur appui btn de gauche

uint32_t textColor[5] = {0x001F, 0xAFE5, 0x07FF, 0x7BE0, 0xFFFF};

int scanTime = 1.5; //In seconds
BLEScan *pBLEScan;

TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h

// Result of ble mi temp
class BLEResult
{
public:
  double temperature = -200.0f;
  double humidity = -1.0f;
  int16_t battery_level = -1;
};

BLEResult result;

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi()
{
  // We start by connecting to a WiFi network

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {

      delay(500);
      displayResult();
      //Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }

  displayResult();
}

void callback(char *topic, byte *message, unsigned int length)
{
}

void reconnect()
{
  Serial.print("reconnect...");

  // Loop until we're reconnected
  while (!client.connected())
  {
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    // Attempt to connect
    if (client.connect(clientId.c_str()))
    {
      Serial.println("ok");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void displayResult()
{
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(100, 60);
  tft.println("HOME");

  if (result.temperature > -200.0f)
  {
    Serial.printf("temperature: %.2f", result.temperature);
    Serial.println();

    tft.setCursor(80, 90);
    tft.println("T:");
    tft.setCursor(110, 90);
    tft.println(result.temperature);
  }
  if (result.humidity > -1.0f)
  {
    Serial.printf("humidity: %.2f", result.humidity);
    Serial.println();

    tft.setCursor(80, 120);
    tft.println("H:");
    tft.setCursor(110, 120);
    tft.println(result.humidity);
  }
  if (result.battery_level > -1)
  {
    Serial.printf("battery_level: %d", result.battery_level);
    Serial.println();

    tft.setCursor(80, 150);
    tft.println("Power:");
    tft.setCursor(150, 150);
    tft.println(result.battery_level);
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    tft.setCursor(65, 200);
    tft.println("Connected");
  }
  else
  {
    tft.setCursor(110, 200);
    tft.println("No");
    tft.setCursor(65, 220);
    tft.println("Connected");
  }
}

// Callback when find device ble
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    BLEAddress address = advertisedDevice.getAddress();

    // Filter by mac address of mi temp
    if (address.toString() == "58:2d:34:3b:7d:3c")
    {

      uint8_t *payloadRaw = advertisedDevice.getPayload();
      size_t payloadLength = advertisedDevice.getPayloadLength();

      Serial.println();
      Serial.println("################################");
      Serial.print("Raw: ");

      // For each byte of ble advertise
      for (int i = 0; i < payloadLength; i++)
      {
        // Show the byte
        Serial.printf("%02X ", payloadRaw[i]);

        // Need min 3 char to start to check
        if (i > 3)
        {
          uint8_t raw = payloadRaw[i - 3];     // type
          uint8_t check = payloadRaw[i - 2];   // must always be 0x10
          int data_length = payloadRaw[i - 1]; // length of data

          if (check == 0x10)
          {
            // temperature, 2 bytes, 16-bit signed integer (LE), 0.1 °C
            if ((raw == 0x04) && (data_length == 2) && (i + data_length <= payloadLength))
            {
              const int16_t temperature = uint16_t(payloadRaw[i + 0]) | (uint16_t(payloadRaw[i + 1]) << 8);
              result.temperature = temperature / 10.0f;
            }
            // humidity, 2 bytes, 16-bit signed integer (LE), 0.1 %
            else if ((raw == 0x06) && (data_length == 2) && (i + data_length <= payloadLength))
            {
              const int16_t humidity = uint16_t(payloadRaw[i + 0]) | (uint16_t(payloadRaw[i + 1]) << 8);
              result.humidity = humidity / 10.0f;
            }
            // battery, 1 byte, 8-bit unsigned integer, 1 %
            else if ((raw == 0x0A) && (data_length == 1) && (i + data_length <= payloadLength))
            {
              result.battery_level = payloadRaw[i + 0];
            }
            // temperature + humidity, 4 bytes, 16-bit signed integer (LE) each, 0.1 °C, 0.1 %
            else if ((raw == 0x0D) && (data_length == 4) && (i + data_length <= payloadLength))
            {
              const int16_t temperature = uint16_t(payloadRaw[i + 0]) | (uint16_t(payloadRaw[i + 1]) << 8);
              const int16_t humidity = uint16_t(payloadRaw[i + 2]) | (uint16_t(payloadRaw[i + 3]) << 8);
              result.temperature = temperature / 10.0f;
              result.humidity = humidity / 10.0f;
            }
          }
        }
      }

      Serial.println();
      displayResult();

      Serial.println("################################");
    }
  }
};

void initDevice()
{
  tft.begin();
  tft.setRotation(1);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(80, 80);
  tft.println("Hello");
  tft.setCursor(80, 120);
  tft.println("DomoTic");
  tft.setCursor(80, 170);
  tft.println("Ver1.00");
}
void gestionAppuiBtnGauche()
{
  cptBtnG = cptBtnG + 1;
  if (cptBtnG > 4)
  {
    cptBtnG = 0;
  }
  tft.setTextColor(textColor[cptBtnG]);
}

void setup()
{
  // Monitor speed
  Serial.begin(115200);

  // Initialise screen
  initDevice();

  // Initialise wifi
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Initialise BLE scan
  Serial.println("Scanning...");
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); // less or equal setInterval value

  // gestion appuie btn
  attachInterrupt(BUTTONGAUCHE, gestionAppuiBtnGauche, FALLING);
}

void loop()
{
  setup_wifi();

  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  if (result.temperature > -200.0f && WiFi.status() == WL_CONNECTED)
  {
    char c[10];
    sprintf(c, "%.2f", result.temperature);
    client.publish(mqtt_topic_temp, c);
    Serial.println("Sent temperature");
  }
  if (result.humidity > -1.0f && WiFi.status() == WL_CONNECTED)
  {
    char c[10];
    sprintf(c, "%.2f", result.humidity);
    client.publish(mqtt_topic_hum, c);
    Serial.println("Sent humidity");
  }
  if (result.battery_level > -1 && WiFi.status() == WL_CONNECTED)
  {
    char c[10];
    sprintf(c, "%d", result.battery_level);
    client.publish(mqtt_topic_bat, c);
    Serial.println("Sent battery_level");
  }

  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);

  pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory
}
