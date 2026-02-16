#include <SPI.h>

#define OLED_CS   10
#define OLED_DC    9
#define OLED_RST   8

void oledCommand(uint8_t cmd) {
  digitalWrite(OLED_DC, LOW);   // Command
  digitalWrite(OLED_CS, LOW);
  SPI.transfer(cmd);
  digitalWrite(OLED_CS, HIGH);
}

void setup() {
  pinMode(OLED_CS, OUTPUT);
  pinMode(OLED_DC, OUTPUT);
  pinMode(OLED_RST, OUTPUT);

  digitalWrite(OLED_CS, HIGH);

  // Reset OLED
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH);


  // ---- SSD1306 Init (minimal!) ----
  oledCommand(0xAE); // Display OFF
  oledCommand(0xA6); // Normal display
  oledCommand(0xAF); // Display ON
}

void loop() {
  // nichts – wenn das OLED lebt, bleibt es an
}
