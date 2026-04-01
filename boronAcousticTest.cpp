#include "Particle.h"
#include <SPI.h>
#include <SdFat.h>

SYSTEM_MODE(AUTOMATIC);

#define DE_RE         2
#define SD_CS         D5
#define DATA_FILE     "testdata.csv"
#define PROGRESS_FILE "progress.txt"
#define PUBLISH_EVENT "sensor_data"

SdFat sd;
String serialBuffer = "";
unsigned long lastPublish = 0;
bool sdReady = false;
bool allDataReceived = false;

bool initSD() {
    if (!sd.begin(SD_CS, SD_SCK_MHZ(1))) {
        Serial.println("SD init failed");
        return false;
    }
    return true;
}

uint32_t readProgress() {
    SdFile f;
    if (!f.open(PROGRESS_FILE, O_READ)) return 0;
    char buf[12];
    int n = f.read(buf, sizeof(buf) - 1);
    f.close();
    if (n <= 0) return 0;
    buf[n] = '\0';
    return (uint32_t)atol(buf);
}

void saveProgress(uint32_t offset) {
    SdFile f;
    if (!f.open(PROGRESS_FILE, O_WRITE | O_CREAT | O_TRUNC)) {
        Serial.println("Could not save progress");
        return;
    }
    f.print(offset);
    f.close();
}

void appendToSD(String line) {
    if (!sdReady) return;
    SdFile f;
    if (!f.open(DATA_FILE, O_WRITE | O_CREAT | O_APPEND)) {
        Serial.println("Could not open data file for writing");
        return;
    }
    f.println(line);
    f.close();
    Serial.println("Saved to SD: " + line);
}

void publishRow(String line) {
    if (line.length() == 0) return;

    String fields[7];
    int fieldIndex = 0;
    int startIndex = 0;

    for (int i = 0; i <= (int)line.length(); i++) {
        if (i == (int)line.length() || line.charAt(i) == ',') {
            fields[fieldIndex++] = line.substring(startIndex, i);
            startIndex = i + 1;
            if (fieldIndex >= 7) break;
        }
    }

    String payload = "{";
    payload += "\"Date\":\""     + fields[0] + "\",";
    payload += "\"Address\":\""  + fields[1] + "\",";
    payload += "\"Temp\":\""     + fields[2] + "\",";
    payload += "\"Ph\":\""       + fields[3] + "\",";
    payload += "\"Cond\":\""     + fields[4] + "\",";
    payload += "\"Do\":\""       + fields[5] + "\",";
    payload += "\"Checksum\":\"" + fields[6] + "\"";
    payload += "}";

    Particle.publish(PUBLISH_EVENT, payload, PRIVATE);
    lastPublish = millis();
    Serial.println("Published: " + payload);
}

void publishNextFromSD() {
    if (!sdReady) return;

    SdFile f;
    if (!f.open(DATA_FILE, O_READ)) return;

    uint32_t offset = readProgress();
    uint32_t fileSize = f.fileSize();

    if (offset >= fileSize) {
        f.close();
        // Only delete files if all data has been received
        if (allDataReceived) {
            if (sd.remove(DATA_FILE)) Serial.println("Data file deleted.");
            if (sd.remove(PROGRESS_FILE)) Serial.println("Progress file deleted.");
            allDataReceived = false;
        }
        return;
    }

    f.seekSet(offset);

    String line = "";
    while (f.available()) {
        char c = f.read();
        if (c == '\n') break;
        if (c != '\r') line += c;
    }

    uint32_t newOffset = f.curPosition();
    f.close();

    line.trim();
    if (line.length() > 0) {
        publishRow(line);
        saveProgress(newOffset);
    } else {
        saveProgress(newOffset);
    }
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(9600);

    pinMode(DE_RE, OUTPUT);
    digitalWrite(DE_RE, LOW);

    sdReady = initSD();
    if (sdReady) {
        Serial.println("SD ready. Progress offset: " + String(readProgress()));
    }

    waitFor(Particle.connected, 30000);
    Serial.println("Ready, listening for acoustic modem data...");
}

void loop() {
    while (Serial1.available()) {
        char c = Serial1.read();

        if (c == '\n' || c == '\r') {
            serialBuffer.trim();
            if (serialBuffer.length() > 0) {
                Serial.println("Raw from modem: " + serialBuffer);

                if (serialBuffer.startsWith("#B") && serialBuffer.length() > 7) {
                    String data = serialBuffer.substring(7);
                    data.trim();

                    if (data == "DONE") {
                        Serial.println("All data received, starting publish...");
                        allDataReceived = true;
                    } else if (data.length() > 0) {
                        Serial.println("Saving to SD: " + data);
                        appendToSD(data);
                    }
                } else {
                    Serial.println("Ignored: " + serialBuffer);
                }
                serialBuffer = "";
            }
        } else {
            serialBuffer += c;
        }
    }

    if (allDataReceived && millis() - lastPublish >= 1100) {
        publishNextFromSD();
    }
}