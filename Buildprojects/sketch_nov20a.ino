#include <WiFi.h>
#include <WebServer.h>

// Access Point Daten
const char* ssid = "BND";
const char* password = "PASSWORT";  // mindestens 8 Zeichen

WebServer server(80);

// RGB Pins
const int redPin = 16;
const int greenPin = 17;
const int bluePin = 18;

// Aktuelle Farben
int r = 0, g = 0, b = 0;

// Funktion, um Farben zu setzen
void setColor(int red, int green, int blue){
  analogWrite(redPin, red);
  analogWrite(greenPin, green);
  analogWrite(bluePin, blue);
}

// Root-Seite: Webinterface
void handleRoot() {
  String html = "<html><body>";
  html += "<h1>ESP32 RGB LED</h1>";
  html += "<p>Farbe einstellen:</p>";
  html += "<a href='/color?r=255&g=0&b=0'><button>Rot</button></a> ";
  html += "<a href='/color?r=0&g=255&b=0'><button>Grün</button></a> ";
  html += "<a href='/color?r=0&g=0&b=255'><button>Blau</button></a> ";
  html += "<a href='/color?r=255&g=255&b=255'><button>Weiß</button></a> ";
  html += "<a href='/color?r=0&g=0&b=0'><button>Aus</button></a> ";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Handler für Farbwechsel
void handleColor() {
  if (server.hasArg("r")) r = server.arg("r").toInt();
  if (server.hasArg("g")) g = server.arg("g").toInt();
  if (server.hasArg("b")) b = server.arg("b").toInt();
  setColor(r, g, b);
  handleRoot();  // Seite neu laden
}

void setup() {
  Serial.begin(115200);

  // Pins als Ausgang
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  // Access Point starten
  WiFi.softAP(ssid, password);
  Serial.print("AP gestartet! Verbinde dich mit WLAN: ");
  Serial.println(ssid);
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.softAPIP());

  // Webserver-Routen
  server.on("/", handleRoot);
  server.on("/color", handleColor);
  server.begin();
  Serial.println("Webserver gestartet!");
}

void loop() {
  server.handleClient();
}
