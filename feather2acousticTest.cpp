#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#define SD_CS        4
#define DATA_FILE    "data.csv"
#define SERVO_PIN    6

String rs485Buffer = "";
unsigned long lastReceiveTime = 0;
bool hasReceivedData = false;
bool doneSent = false;
bool sdReady = false;

void appendToSD(String line) {
    if (!sdReady) return;
    File f = SD.open(DATA_FILE, FILE_WRITE);
    if (!f) {
        Serial.println("Could not open data file");
        return;
    }
    f.println(line);
    f.close();
    Serial.println("Saved to SD: " + line);
}

void sendAcoustic(String data) {
    int len = data.length();
    if (len > 64) {
        Serial.println("WARNING: payload too long, truncating");
        data = data.substring(0, 64);
        len = 64;
    }

    String lenStr = (len < 10) ? "0" + String(len) : String(len);
    String command = "$B" + lenStr + data;

    Serial.println("Sending acoustic: " + command);

    Serial1.end();
    Serial1.begin(9600);
    Serial1.print(command);

    float txTime = 0.105 + (len + 16) * 0.0125;
    delay((int)(txTime * 1000) + 2000);

    Serial1.end();
    Serial1.begin(9600);
}

void sendAllFromSD() {
    if (!sdReady) return;
    if (!SD.exists(DATA_FILE)) return;

    File f = SD.open(DATA_FILE, FILE_READ);
    if (!f) return;

    Serial.println("Sending all data acoustically...");

    String line = "";
    while (f.available()) {
        char c = f.read();
        if (c == '\n') {
            line.trim();
            if (line.length() > 0) {
                sendAcoustic(line);
            }
            line = "";
        } else if (c != '\r') {
            line += c;
        }
    }

    // Handle last line if no trailing newline
    line.trim();
    if (line.length() > 0) {
        sendAcoustic(line);
    }

    f.close();

    // Delete file after sending
    SD.remove(DATA_FILE);
    Serial.println("Data file deleted.");

    // Send DONE marker
    Serial.println("Sending DONE marker");
    sendAcoustic("DONE");
    doneSent = true;
    hasReceivedData = false;
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(9600);

    pinMode(SERVO_PIN, OUTPUT);

    if (!SD.begin(SD_CS)) {
        Serial.println("SD init failed");
    } else {
        sdReady = true;
        Serial.println("SD ready");
        // Clean up any leftover file from previous run
        if (SD.exists(DATA_FILE)) {
            SD.remove(DATA_FILE);
            Serial.println("Cleared leftover data file");
        }
    }

    Serial.println("Feather 2 ready, listening for RS-485 data...");
}

void loop() {
    // Read all incoming RS-485 and save to SD
    while (Serial1.available()) {
        char c = Serial1.read();

        if (c == '\n' || c == '\r') {
            rs485Buffer.trim();
            if (rs485Buffer.length() > 0) {
                Serial.println("Received via RS-485: " + rs485Buffer);
                appendToSD(rs485Buffer);
                rs485Buffer = "";
                hasReceivedData = true;
                doneSent = false;
                lastReceiveTime = millis();
            }
        } else {
            rs485Buffer += c;
            lastReceiveTime = millis();
        }
    }

    // After 3 seconds of RS-485 silence, send everything acoustically
    if (hasReceivedData && !doneSent && millis() - lastReceiveTime > 3000) {
        sendAllFromSD();
    }
}