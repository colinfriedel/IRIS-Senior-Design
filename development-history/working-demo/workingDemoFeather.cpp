#include <Servo.h>
#include <SPI.h>
#include <Adafruit_MCP2515.h>
#include <SD.h>
#include <Arduino.h>
#include <wiring_private.h>


// --- FORWARD DECLARATIONS ---
void handleRS485Logging();
void writeReg(uint8_t reg, uint16_t value);
uint16_t readReg(uint8_t reg);
void freespin();
void Turn(int turns);
void dumpDataToAcoustic();
void sendAcoustic(String data);
String trimFloat(String val, int decimals);
String trimLine(String line);


// --- SERCOM SERIAL HANDLING ---
Uart Serial2(&sercom1, 12, 10, SERCOM_RX_PAD_3, UART_TX_PAD_2);
void SERCOM1_Handler() {
    Serial2.IrqHandler();
}


// --- HARDWARE CONFIG ---
const int CAN_CS_PIN = 5;
const long CAN_BAUD = 1000000;
const int CAN_ARB_ID = 0x0;
const uint8_t SERVO_ID = 1;


Adafruit_MCP2515 mcp(CAN_CS_PIN);


const int AdaloggerPin = 4;
const int DeployedTurns = 5;
const int RetractedTurns = 0;
const int OpenMS = 1500;
const int CloseMS = 1800;
const int turnDelay = 1000;


Servo Brake;


char currentLogFile[16] = "LOG_00.csv";
String currentCollectionTime = "2026-00-00 00:00";


enum SystemState {
    STATE_LISTEN,
    STATE_DUMP,
    STATE_DEPLOY,
    STATE_WAIT_RETRACT,
    STATE_RETRACT
};


SystemState currentState = STATE_LISTEN;


void smartWait(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        handleRS485Logging();
    }
}


// --- SETUP ---
void setup() {
    delay(5000);
    Serial.begin(115200);
    Serial1.begin(9600);
    Serial2.begin(9600);
    pinPeripheral(10, PIO_SERCOM);
    pinPeripheral(12, PIO_SERCOM);


    pinMode(AdaloggerPin, OUTPUT);
    digitalWrite(AdaloggerPin, LOW);
    pinMode(6, OUTPUT);
    digitalWrite(6, LOW);


    Serial.println("Initializing SD Card...");
    if (!SD.begin(AdaloggerPin)) {
        Serial.println("SD init failed!");
    } else {
        Serial.println("SD OK!");
        updateFileName();
        Serial.print("Current log file: ");
        Serial.println(currentLogFile);
    }


    Serial.println("Initializing CAN...");
    if (!mcp.begin(CAN_BAUD)) {
        Serial.println("CAN init failed!");
        while (1) delay(10);
    }
    Serial.println("CAN OK!");
    Serial.println("System Ready. Entering Listen State.");
}


// --- MAIN LOOP FSM ---
void loop() {
    switch (currentState) {

        case STATE_LISTEN:
            handleRS485Logging();

            if (Serial1.available()) {
                String cmd = Serial1.readStringUntil('\n');
                Serial.println("Received: " + cmd);
                // Strip modem wrapper if present
                if (cmd.startsWith("$B") && cmd.length() > 7) {
                    cmd = cmd.substring(7);
                }
                if (cmd.indexOf("DEPLOY") >= 0) {
                    Serial.println("DEPLOY received, moving to DUMP state.");
                    currentState = STATE_DUMP;
                }
            }
            break;


        case STATE_DUMP:
            dumpDataToAcoustic();
            currentState = STATE_DEPLOY;
            break;


        case STATE_DEPLOY:
            Serial.println("Opening brake...");
            Brake.attach(9);
            Brake.writeMicroseconds(OpenMS);
            smartWait(5000);


            Serial.println("Deploying...");
            Turn(DeployedTurns);
            smartWait(DeployedTurns * turnDelay);


            readReg(0x18);
            freespin();
            //Serial.println("Closing Break...");
            //Brake.writeMicroseconds(CloseMS);
            //smartWait(5000);
            //Brake.detach();


            // Send DEPLOYED confirmation acoustically
            Serial.println("Sending DEPLOYED confirmation...");
            sendAcoustic("DEPLOYED");


            currentState = STATE_WAIT_RETRACT;
            break;


        case STATE_WAIT_RETRACT:
            if (Serial1.available()) {
                String cmd = Serial1.readStringUntil('\n');
                Serial.println("Read: " + cmd);
                if (cmd.indexOf("RETRACT") >= 0) {
                    currentState = STATE_RETRACT;
                }
            }
            break;


        case STATE_RETRACT:
            Serial.println("Retracting...");
            Turn(RetractedTurns);
            smartWait(DeployedTurns * turnDelay);

            Serial.println("Closing Break...");
            Brake.writeMicroseconds(CloseMS);
            smartWait(5000);
            Brake.detach();

            readReg(0x18);
            freespin();

            updateFileName();
            Serial.println("Done. Returning to Listen State.");
            currentState = STATE_LISTEN;
            break;
    }
}


// --- CORE TASKS ---


void handleRS485Logging() {
    if (Serial2.available() > 0) {
        uint8_t firstByte = Serial2.peek();

        // --- CASE 1: Date/Time String (Starts with 0xFF) ---
        if (firstByte == 0xFF) {
            Serial2.read(); // Discard the 0xFF identifier
            String newTime = Serial2.readStringUntil('\n');
            newTime.trim();
            if (newTime.length() > 5) {
                currentCollectionTime = newTime;
                Serial.print(">>> Timestamp Synchronized: ");
                Serial.println(currentCollectionTime);
            }
        }

        // --- CASE 2: Binary Data Packet (Starts with 0xAA) ---
        else if (firstByte == 0xAA) {
            if (Serial2.available() >= 23) {
                uint8_t header[3];
                header[0] = Serial2.read(); // 0xAA
                header[1] = Serial2.read(); // Address
                header[2] = Serial2.read(); // Type (0xBB = data, 0xCC = signal)

                if (header[2] == 0xBB) {
                    float vals[5]; // Temp, pH, Cond, DO, Checksum
                    for (int i = 0; i < 5; i++) {
                        uint8_t b[4];
                        Serial2.readBytes(b, 4);
                        memcpy(&vals[i], b, 4);
                    }

                    // Verify checksum
                    float expectedChecksum = vals[0] + vals[1] + vals[2] + vals[3] 
                                           + header[0] + header[1] + header[2];
                    if (vals[4] == expectedChecksum) {
                        File logFile = SD.open(currentLogFile, FILE_WRITE);
                        if (logFile) {
                            logFile.print(currentCollectionTime);
                            logFile.print(",");
                            logFile.print(header[1]);   // Address
                            logFile.print(",");
                            logFile.print(vals[0], 2);  // Temp
                            logFile.print(",");
                            logFile.print(vals[1], 2);  // pH
                            logFile.print(",");
                            logFile.print(vals[2], 2);  // Cond
                            logFile.print(",");
                            logFile.print(vals[3], 2);  // DO
                            logFile.print(",");
                            logFile.print(vals[4], 2);  // Checksum
                            logFile.println();
                            logFile.close();
                            Serial.print(">>> Logged: ");
                            Serial.print(currentCollectionTime);
                            Serial.print(" | Addr: ");
                            Serial.print(header[1]);
                            Serial.print(" | DO: ");
                            Serial.println(vals[3]);
                        } else {
                            Serial.println(">>> ERROR: Could not open log file!");
                        }
                    } else {
                        Serial.print(">>> Checksum mismatch, discarding. Got: ");
                        Serial.print(vals[4]);
                        Serial.print(" Expected: ");
                        Serial.println(expectedChecksum);
                    }
                } else {
                    // Signal frame (0xCC) — flush until next packet
                    while (Serial2.available() > 0 && 
                           Serial2.peek() != 0xAA && 
                           Serial2.peek() != 0xFF) {
                        Serial2.read();
                    }
                }
            }
        }
        else {
            Serial2.read(); // Discard noise
        }
    }
}

void updateFileName() {
    SD.begin(AdaloggerPin);
    for (uint8_t i = 0; i < 100; i++) {
        currentLogFile[4] = i / 10 + '0';
        currentLogFile[5] = i % 10 + '0';
        if (!SD.exists(currentLogFile)) {
            File logFile = SD.open(currentLogFile, FILE_WRITE);
            if (logFile) {
                logFile.println("TIME,ADDRESS,TEMP,PH,COND,DO,CHECKSUM");
                logFile.close();
                Serial.println("SD: Created new log -> " + String(currentLogFile));
            }
            break;
        }
    }
}


// Wrap data in $B acoustic broadcast command and send
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


    // Switch Serial1 to modem TX
    //Serial1.end();
    //Serial1.begin(9600);
    Serial1.print(command);
    Serial1.flush();


    // Wait for acoustic transmission to complete
    float txTime = 0.105 + (len + 16) * 0.0125;
    delay((int)(txTime * 1000) + 2000);


    // Switch Serial1 back to receive
    //Serial1.end();
    //Serial1.begin(9600);
}


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


void dumpDataToAcoustic() {
    Serial.println("Dumping data over acoustic modem...");
    Serial.println("Looking for file: " + String(currentLogFile));
    
    if (!SD.exists(currentLogFile)) {
        Serial.println("File does not exist!");
        sendAcoustic("DONE");
        return;
    }

    File logFile = SD.open(currentLogFile, FILE_READ);
    if (!logFile) {
        Serial.println("Could not open file!");
        sendAcoustic("DONE");
        return;
    }

    Serial.println("File size: " + String(logFile.size()));
    
    if (logFile.size() == 0) {
        Serial.println("File is empty!");
        logFile.close();
        sendAcoustic("DONE");
        return;
    }

    String line = "";
    bool firstLine = true;

    while (logFile.available()) {
        char c = logFile.read();
        if (c == '\n') {
            line.trim();
            if (line.length() > 0) {
                if (firstLine) {
                    firstLine = false;
                    Serial.println("Skipping header: " + line);
                } else {
                    String trimmed = trimLine(line);
                    if (trimmed.length() > 0) {
                        sendAcoustic(trimmed);
                    }
                }
            }
            line = "";
        } else if (c != '\r') {
            line += c;
        }
    }

    line.trim();
    if (line.length() > 0 && !firstLine) {
        sendAcoustic(trimLine(line));
    }

    logFile.close();
    Serial.println("Dump complete. Sending DONE marker.");
    delay(3000);
    sendAcoustic("DONE");
}


// --- CAN SERVO COMMANDS ---
void freespin() {
    writeReg(0x46, 0x0200);
}


void Turn(int turns) {
    writeReg(0x46, 0x0000);
    writeReg(0x24, (uint16_t)turns);
}


void writeReg(uint8_t reg, uint16_t value) {
    uint8_t lo = value & 0xFF;
    uint8_t hi = (value >> 8) & 0xFF;


    digitalWrite(AdaloggerPin, HIGH);
    mcp.beginPacket(CAN_ARB_ID);
    mcp.write((uint8_t)'w');
    mcp.write(SERVO_ID);
    mcp.write(reg);
    mcp.write(lo);
    mcp.write(hi);
    mcp.endPacket();
    digitalWrite(AdaloggerPin, LOW);
}


uint16_t readReg(uint8_t reg) {
    digitalWrite(AdaloggerPin, HIGH);
    mcp.beginPacket(CAN_ARB_ID);
    mcp.write('r');
    mcp.write(SERVO_ID);
    mcp.write(reg);
    mcp.endPacket();
    digitalWrite(AdaloggerPin, LOW);


    uint32_t start = millis();
    while (millis() - start < 200) {
        int len = mcp.parsePacket();
        if (!len) continue;


        uint8_t b[8];
        int i = 0;
        while (mcp.available() && i < (int)sizeof(b)) {
            b[i++] = mcp.read();
        }


        if (i >= 5 && b[0] == 'v' && b[1] == SERVO_ID && b[2] == reg) {
            uint16_t val = (uint16_t)b[3] | ((uint16_t)b[4] << 8);
            Serial.println(val);
            return val;
        }
    }


    Serial.println("ERR");
    return 0;
}
