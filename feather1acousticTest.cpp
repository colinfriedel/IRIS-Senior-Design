#include <SPI.h>
#include <SD.h>

#define SD_CS 4
#define DE_RE 2

String trimFloat(String val, int decimals) {
    val.trim();
    int dotIndex = val.indexOf('.');
    if (dotIndex == -1) return val;
    if (decimals == 0) return val.substring(0, dotIndex);
    int endIndex = dotIndex + 1 + decimals;
    if (endIndex >= (int)val.length()) return val;
    return val.substring(0, endIndex);
}

String trimLine(String line) {
    if (line.length() == 0) return "";

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

    fields[2] = trimFloat(fields[2], 2);
    fields[3] = trimFloat(fields[3], 2);
    fields[4] = trimFloat(fields[4], 0);
    fields[5] = trimFloat(fields[5], 2);

    return fields[0] + "," + fields[1] + "," + fields[2] + "," +
           fields[3] + "," + fields[4] + "," + fields[5] + "," + fields[6];
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(9600);

    pinMode(DE_RE, OUTPUT);
    digitalWrite(DE_RE, HIGH);  // always transmit

    if (!SD.begin(SD_CS)) {
        Serial.println("SD card init failed");
        while (1);
    }
    Serial.println("SD card initialized");
}

void loop() {
    File f = SD.open("BETHAN~1.CSV");
    if (!f) {
        Serial.println("Failed to open file");
        while (1);
    }

    Serial.println("Sending file over RS-485...");

    String line = "";
    bool firstLine = true;

    while (f.available()) {
        char c = f.read();

        if (c == '\n' || c == '\r') {
            line.trim();
            if (line.length() > 0) {
                if (firstLine) {
                    firstLine = false;
                    Serial.println("Skipping header: " + line);
                } else {
                    String trimmed = trimLine(line);
                    Serial.println("Sending: " + trimmed);
                    Serial1.println(trimmed);
                    delay(100);
                }
            }
            line = "";
        } else {
            line += c;
        }
    }

    line.trim();
    if (line.length() > 0 && !firstLine) {
        String trimmed = trimLine(line);
        Serial.println("Sending: " + trimmed);
        Serial1.println(trimmed);
        delay(100);
    }

    f.close();
    Serial.println("File sent.");
    while (1);
}