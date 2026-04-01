#include "Particle.h"

SYSTEM_MODE(AUTOMATIC);

#define DE_RE 2   // RS-485 DE/RE control pin

void setup() {
  Serial.begin(115200);   // USB serial monitor
  Serial1.begin(9600);    // RS-485 receive

  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, LOW);   // LOW = receive mode
}

void loop() {
  while (Serial1.available()) {
    char c = Serial1.read();
    Serial.print(c);           // print to USB monitor
  }
}