#include "Particle.h"
#include <SPI.h>
#include <SdFat.h>

#define SD_CS A5
#define FILE_NAME "bethany711.csv"
#define PUBLISH_EVENT "sensor_data"

SdFat sd;
SdFile f;

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
    payload += "\"Date\":\"" + fields[0] + "\",";
    payload += "\"Address\":\"" + fields[1] + "\",";
    payload += "\"Temp\":\"" + fields[2] + "\",";
    payload += "\"Ph\":\"" + fields[3] + "\",";
    payload += "\"Cond\":\"" + fields[4] + "\",";
    payload += "\"Do\":\"" + fields[5] + "\",";
    payload += "\"Checksum\":\"" + fields[6] + "\"";
    payload += "}";

    Particle.publish(PUBLISH_EVENT, payload, PRIVATE);
    delay(1100);
}

void readAndPublishSD() {
    if (!sd.begin(SD_CS, SD_SCK_MHZ(1))) {
        Particle.publish("sd_status", "SD init failed", PRIVATE);
        return;
    }

    if (!f.open(FILE_NAME, O_READ)) {
        Particle.publish("sd_status", "File open failed", PRIVATE);
        return;
    }

    String line = "";
    bool firstLine = true;

    while (f.available()) {
        char c = f.read();

        if (c == '\n' || c == '\r') {
            line.trim();
            if (line.length() > 0) {
                if (firstLine) {
                    firstLine = false;
                } else {
                    publishRow(line);
                }
            }
            line = "";
        } else {
            line += c;
        }
    }

    line.trim();
    if (line.length() > 0 && !firstLine) {
        publishRow(line);
    }

    f.close();
    Particle.publish("sd_status", "Done", PRIVATE);
}

int triggerRead(String arg) {
    readAndPublishSD();
    return 1;
}

void setup() {
    Serial.begin(115200);
    waitFor(Serial.isConnected, 5000);
    waitFor(Particle.connected, 30000);

    Particle.function("readSD", triggerRead);

    Serial.println("Ready.");
    readAndPublishSD();
}

void loop() {
}