#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Firebase_ESP_Client.h>
#include <WiFiClientSecure.h>

// OLED setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Firebase credentials
#define FIREBASE_URL "http://your_firebase_url"
#define FIREBASE_API_KEY "your_api_key"
#define FIREBASE_PATH "/display/text"
#define FIREBASE_USER_EMAIL "your@gmail.com"
#define FIREBASE_USER_PASSWORD "yourpassword"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Web server
WebServer server(80);

// EEPROM setup
String ssid, pass, devid, content;
bool apmode = false;
bool scanComplete = false;
String scannedNetworks = "<p>Scanning networks...</p>";

// OLED scrolling
String FireBaseMsg = "";
int16_t scrollX = SCREEN_WIDTH;
unsigned long lastScroll = 0;
unsigned long lastFirebaseCheck = 0;

// Setup - Check for oled, wifi and firebase connections
void setup() {
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED init failed"));
    while (true);
  }

  displayOLEDmsg("Booting...\nWaiting 5s");
  delay(5000);

  readData();

  if (ssid.length() == 0) {
    displayOLEDmsg("No SSID Found\nEntering AP Mode");
    ap_mode();
    return;
  }

  pinMode(0, INPUT_PULLUP);

  if (digitalRead(0) == 0) {
    displayOLEDmsg("Button Pressed\nAP Mode");
    ap_mode();
  } else {
    if (testWifi()) {
      // Firebase setup
      config.api_key = FIREBASE_API_KEY;
      config.database_url = FIREBASE_URL;
      auth.user.email = FIREBASE_USER_EMAIL;
      auth.user.password = FIREBASE_USER_PASSWORD;

      Firebase.begin(&config, &auth);
      Firebase.reconnectWiFi(true);

      // Firebase readiness check
      Serial.println("Connecting to Firebase..");
      displayOLEDmsg("WiFi Connected\nConnecting Firebase..");
      delay(10000);

      Serial.println("Firebase setup complete.");
    } else {
      displayOLEDmsg("WiFi Failed.\nEntering AP Mode...");
      ap_mode();
    }
  }
}

//Loop - if in apmode: scan network, if not in apmode: connect firebase and display creds and text
void loop() {
  if (apmode) {
    server.handleClient();

    if (!scanComplete) {
      int n = WiFi.scanComplete();
      if (n == -2) WiFi.scanNetworks(true); // If no scan, initiate
      else if (n >= 0) {
        // Build HTML table from scanned networks
        scannedNetworks = "<table border='1' style='width:100%; border-collapse:collapse;'>";
        scannedNetworks += "<tr><th>SSID</th><th>Signal Strength</th></tr>";
        for (int i = 0; i < n; ++i) {
          scannedNetworks += "<tr><td>" + WiFi.SSID(i) + "</td><td>" + String(WiFi.RSSI(i)) + " dBm</td></tr>";
        }
        scannedNetworks += "</table>";
        scanComplete = true;  // Prevent further scanning
        WiFi.scanDelete();  // Free memory
      }
    }
  } else {
     if (millis() - lastScroll > 30) {
      lastScroll = millis();

      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);

      display.setCursor(0, 0);
      display.print("WiFi Connected!");

      display.setCursor(0, 8);
      display.print("SSID: " + ssid);

      display.setCursor(0, 16);
      display.print("DevID: " + devid);

      // Scroll the Firebase message
      display.setCursor(scrollX, 24);
      display.print(FireBaseMsg);
      display.display();

      scrollX--;  // Move left
      int16_t textWidth = FireBaseMsg.length() * 6;  // 6px per char @ size 1
      if (scrollX < -textWidth) {
        scrollX = SCREEN_WIDTH;  // Reset to right side
      }
    }

    // Fetch Firebase message periodically
    if (millis() - lastFirebaseCheck > 5000) {
      if (Firebase.ready() && Firebase.RTDB.getString(&fbdo, FIREBASE_PATH)) {
        FireBaseMsg = fbdo.stringData();
        scrollX = SCREEN_WIDTH;
        //Serial.println("New Firebase message: " + FireBaseMsg);
      } else {
        String errorReason = fbdo.errorReason();
        FireBaseMsg = "FireBase Error: " + errorReason.substring(0, 24) + "...";
        scrollX = SCREEN_WIDTH;
        Serial.print("Firebase get failed: ");
        Serial.println(errorReason);
      }
      lastFirebaseCheck = millis();
    }
  }
}

// Oled message format
void displayOLEDmsg(String msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
}

// Read data from eeprom
void readData() {
  EEPROM.begin(512);
  ssid = pass = devid = "";
  for (int i = 0; i < 20; i++) { char ch = EEPROM.read(i); if (isPrintable(ch)) ssid += ch; }
  for (int i = 20; i < 40; i++) { char ch = EEPROM.read(i); if (isPrintable(ch)) pass += ch; }
  for (int i = 40; i < 60; i++) { char ch = EEPROM.read(i); if (isPrintable(ch)) devid += ch; }
  EEPROM.end();

  Serial.println("SSID: " + ssid);
  Serial.println("PASS: " + pass);
  Serial.println("DEV: " + devid);
}

// Write data to eeprom
void writeData(String a, String b, String c) {
  clearData();
  EEPROM.begin(512);
  for (int i = 0; i < 20; i++) EEPROM.write(i, (i < a.length()) ? a[i] : 0);
  for (int i = 0; i < 20; i++) EEPROM.write(20 + i, (i < b.length()) ? b[i] : 0);
  for (int i = 0; i < 20; i++) EEPROM.write(40 + i, (i < c.length()) ? c[i] : 0);
  EEPROM.commit();
  EEPROM.end();
  displayOLEDmsg("EEPROM Updated!");
}

// Clear data
void clearData() {
  EEPROM.begin(512);
  for (int i = 0; i < 60; i++) EEPROM.write(i, 0);
  EEPROM.commit();
  EEPROM.end();
}

// Test wifi connection
boolean testWifi() {
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  int c = 0;
  while (c < 50) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setAutoReconnect(true);
      WiFi.persistent(true);
      Serial.println("Wifi Connected! IP: " + WiFi.localIP().toString());
      return true;
    }
    delay(900);
    c++;
  }
  Serial.println("WiFi connect fail");
  return false;
}

// AP mode
void ap_mode() {
  digitalWrite(2, HIGH);
  apmode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32_eeprom", "");
  WiFi.scanNetworks(true);
  Serial.println("AP IP: " + WiFi.softAPIP().toString());
  displayOLEDmsg("Enter AP Mode:\nSSID: ESP32_eeprom\nIP: 192.168.4.1");
  launchWeb(0);
}

//lauch ap mode website
void launchWeb(int webtype) {
  createWebServer(webtype);
  server.begin();
}

//website for apmode
void createWebServer(int webtype) {
  if (webtype == 0) {
    // Main configuration page
    server.on("/", []() {
      // HTML structure for configuration page
      content = R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
          <meta name="viewport" content="width=device-width, initial-scale=1">
          <style>
            body { font-family: Arial; text-align: center; padding: 20px; background: #f4f4f4; }
            h1 { color: #333; }
            form { background: white; padding: 20px; border-radius: 10px; display: inline-block; }
            input, label { display: block; margin: 10px auto; width: 80%; max-width: 300px; }
            input[type="text"], input[type="password"] {
              padding: 10px; border: 1px solid #ccc; border-radius: 5px; }
            .button {
              background-color: #3CBC8D; color: white; padding: 10px 20px; border: none;
              border-radius: 5px; cursor: pointer; font-size: 16px; }
            table { margin: 10px auto; width: 90%; border: 1px solid #ccc; }
            th, td { padding: 8px; text-align: left; border: 1px solid #ccc; }
          </style>
        </head>
        <body>
          <h1>WiFi Manager</h1>
          <p><strong>Stored SSID:</strong> )rawliteral" + ssid + R"rawliteral(</p>
          <p><strong>Stored Password:</strong> )rawliteral" + pass + R"rawliteral(</p>
          <p><strong>Stored Device ID:</strong> )rawliteral" + devid + R"rawliteral(</p>
          <form method='get' action='setting'>
            <label>WiFi SSID:</label>
            <input type='text' name='ssid' required>
            <label>WiFi Password:</label>
            <input type='password' name='password'>
            <label>Device ID:</label>
            <input type='text' name='devid' required>
            <input class='button' type='submit' value='Save Settings'>
          </form>
          <h3>Available Networks:</h3>
      )rawliteral" + scannedNetworks + R"rawliteral(
        </body>
        </html>
      )rawliteral";

      server.send(200, "text/html", content);
    });

    // Save new WiFi and device ID settings to EEPROM
    server.on("/setting", []() {
      String ssidw = server.arg("ssid");
      String passw = server.arg("password");
      String devidw = server.arg("devid");
      writeData(ssidw, passw, devidw);
      server.send(200, "text/html", "<h2>Settings Saved Successfully</h2><p>The device will now restart to apply the new configuration.</p>");
      delay(2000);
      digitalWrite(2, LOW); // Turn off LED before restart
      ESP.restart();
    });

    // Route to clear all EEPROM data
    server.on("/clear", []() {
      clearData();
      server.send(200, "text/html", "<h2>All Data Cleared</h2><p>The device will restart now. Please reconnect and reconfigure if needed.</p>");
      delay(2000);
      ESP.restart();
    });
  }
}
