#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Servo sunServo;

// --- Pins ---
const int ldrLeft = A0;
const int ldrRight = A1;
const int servoPin = 9;

// --- Steuerungsvariablen ---
int pos = 90;
int threshold = 70;
int tiltRange = 12;
float smoothPos = 90.0;
float smoothFactor = 0.2;

// --- Timer ---
unsigned long lastPrint = 0;
const unsigned long printInterval = 500;

void setup() {
  Serial.begin(9600);
  sunServo.attach(servoPin);
  sunServo.write(pos);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("ERROR"));
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(" Initialisierung...");
  delay(500);
  display.println("Sonnenblume Licht");
  display.display();
  delay(500);
  display.clearDisplay();
}

void loop() {
  int leftValue = analogRead(ldrLeft);
  int rightValue = analogRead(ldrRight);
  int diff = leftValue - rightValue;

  // --- Servo Steuerung ---
  if (abs(diff) > threshold) {
    float targetPos = map(diff, -1023, 1023, 0, 180);
    targetPos = constrain(targetPos, 0, 180);
    smoothPos += (targetPos - smoothPos) * smoothFactor;
    pos = (int)smoothPos;
    sunServo.write(pos);
  }

  // --- OLED Zeichnen ---
  display.clearDisplay();

  int centerX = SCREEN_WIDTH / 2;
  int centerY = 12; // Stiel-Ende höher, Platz für Text

  int tilt = map(pos, 0, 180, -tiltRange, tiltRange);
  int flowerX = centerX + tilt;
  int flowerY = centerY - 6;

  // Stiel
  display.drawLine(centerX, centerY + 6, flowerX, flowerY, SSD1306_WHITE);
  // Blüte
  display.fillCircle(flowerX, flowerY, 4, SSD1306_WHITE);
  display.drawCircle(flowerX, flowerY, 5, SSD1306_WHITE);

  // --- Text unter Blume ---
  display.setCursor(0, 20);
  display.setTextSize(1);
  if (diff > threshold) display.print("LICHT LINKS <-");
  else if (diff < -threshold) display.print("LICHT RECHTS ->");
  else display.print("ZENTRIERT");

  // --- Tilt-Wert rechts anzeigen ---
  display.setCursor(100, 20);
  display.print("T:");
  display.print(tilt);

  // --- LDR-Differenz als Balkenanzeige ---
  int barWidth = map(abs(diff), 0, 1023, 0, SCREEN_WIDTH);
  int barY = SCREEN_HEIGHT - 2;
  if (diff > 0) {
    display.fillRect(centerX, barY, barWidth/2, 2, SSD1306_WHITE); // links hell
  } else if (diff < 0) {
    display.fillRect(centerX - barWidth/2, barY, barWidth/2, 2, SSD1306_WHITE); // rechts hell
  }

  display.display();

  // --- Serielle Ausgabe ---
  if (millis() - lastPrint > printInterval) {
    Serial.print("LDR L: "); Serial.print(leftValue);
    Serial.print(" R: "); Serial.print(rightValue);
    Serial.print(" | Diff: "); Serial.print(diff);
    Serial.print(" | Servo: "); Serial.print(pos);
    Serial.print(" | Tilt: "); Serial.println(tilt);
    lastPrint = millis();
  }

  delay(50); // flüssige Animation
}






/*Bib: 
Adafruit SSD1306

Adafruit GFX Library*/

/*Schaltplan Verbindungen:
Servo:
Rot → 5 V
Braun → GND
Orange → D9
LDR links
Eine Seite → 5 V
Andere Seite → A0
10 kΩ von A0 → GND
LDR rechts
Eine Seite → 5 V
Andere Seite → A1
10 kΩ von A1 → GND
OLED-Display (SSD1306 über I²C)

VCC → 5 V
GND → GND
1 CS => D10
2 SDIN => D11
3 None => -
4 SCLK => D13
5, 11 GND => GND
6, 12 VCC => VCC
7 D/C => D7
8 RES => D8
9 VBATC => -
10 VDDC => -
*/