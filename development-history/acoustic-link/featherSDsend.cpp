#include <SPI.h>
#include <SD.h>

#define SD_CS 4         // built-in microSD slot CS pin
#define DE_RE 2         // RS-485 DE/RE control pin

void setup() {
  Serial.begin(115200);     // USB debug
  Serial1.begin(9600);      // RS-485 TX

  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, HIGH);   // HIGH = transmit

  if (!SD.begin(SD_CS)) {
    Serial.println("SD card init failed");
    while (1);
  }
  Serial.println("SD card initialized");
}

void loop() {
  File f = SD.open("BETHAN~1.CSV"); // short name for CSV
  if (!f) {
    Serial.println("Failed to open file");
    while(1);
  }
  Serial.println("Sending file over RS-485...");

  while (f.available()) {
    char c = f.read();
    Serial1.write(c);       // send each byte to Boron via RS-485
    delay(1);               // tiny delay to prevent overflow
  }

  f.close();
  Serial.println("File sent.");
  while (1);  // stop after one pass for testing
}