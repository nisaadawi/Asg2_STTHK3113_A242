#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Web server on port 80
WebServer server(80);

// EEPROM setup
String ssid, pass, devid, content;
bool apmode = false;
bool scanComplete = false;
String scannedNetworks = "<p>Scanning networks...</p>";

// Setup
void setup() {
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED initialization failed"));
    while (true);
  }

  displayOLEDmsg("Booting...\nWaiting 5s");
  delay(5000);

  readData();

  if (ssid.length() == 0) {
    Serial.println("No SSID found, AP mode");
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
      displayOLEDmsg("WiFi Connected!\nSSID: " + ssid + "\nPassword: " + pass + "\nDevID: " + devid);
    } else {
      displayOLEDmsg("WiFi Failed. entering AP Mode...");
      ap_mode();
    }
  }
}

// Main loop
void loop() {
  if (apmode) {
    server.handleClient();
    if (!scanComplete) {
      int n = WiFi.scanComplete();
      if (n == -2) WiFi.scanNetworks(true);
      else if (n >= 0) {
        scannedNetworks = "<table border='1'><tr><th>SSID</th><th>Signal</th></tr>";
        for (int i = 0; i < n; ++i) {
          scannedNetworks += "<tr><td>" + WiFi.SSID(i) + "</td><td>" + String(WiFi.RSSI(i)) + " dBm</td></tr>";
        }
        scannedNetworks += "</table>";
        scanComplete = true;
        WiFi.scanDelete();
      }
    }
  }
}

// OLED display format
void displayOLEDmsg(String msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
}

// Read Data from EEPROM 
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

  displayOLEDmsg("Success! Read EEPROM:\nSSID: " + ssid + "\nPassword: " + pass + "\nDevID: " + devid);

}

// Write Data into EEPROM 
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

// Clear data in EEPROM 
void clearData() {
  EEPROM.begin(512);
  for (int i = 0; i < 60; i++) EEPROM.write(i, 0);
  EEPROM.commit();
  EEPROM.end();
}

// WiFi test
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
      Serial.println("Connected! IP: " + WiFi.localIP().toString());
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

// Web server
void launchWeb(int webtype) {
  createWebServer(webtype);
  server.begin();
}

// Web routes
void createWebServer(int webtype) {
  if (webtype == 0) {
    server.on("/", []() {
      content = R"rawliteral(
        <html><body>
        <h1>WiFi Manager</h1>
        <form method='get' action='setting'>
          SSID:<br><input name='ssid'><br>
          Password:<br><input name='password' type='password'><br>
          Device ID:<br><input name='devid'><br>
          <input type='submit' value='Save'>
        </form>
        <h3>Networks:</h3>
      )rawliteral" + scannedNetworks + "</body></html>";
      server.send(200, "text/html", content);
    });

    server.on("/setting", []() {
      writeData(server.arg("ssid"), server.arg("password"), server.arg("devid"));
      server.send(200, "text/html", "<h2>Saved! Restarting...</h2>");
      delay(2000);
      digitalWrite(2, LOW);
      displayOLEDmsg("Saved! Restarting...");
      ESP.restart();
    });

    server.on("/clear", []() {
      clearData();
      server.send(200, "text/html", "<h2>Cleared! Restarting...</h2>");
      displayOLEDmsg("EEPROM Cleared\nRestarting...");
      delay(2000);
      ESP.restart();
    });
  }
}
