#include "Particle.h"
#include <SPI.h>
#include <SdFat.h>

SYSTEM_MODE(AUTOMATIC);

#define SD_CS         D5
#define DATA_FILE     "testdata.csv"
#define PROGRESS_FILE "progress.txt"
#define PUBLISH_EVENT "sensor_data"

SdFat sd;
String serialBuffer = "";
unsigned long lastPublish = 0;
bool sdReady = false;
bool allDataReceived = false;
bool deployedConfirmed = false;
bool retractSent = false;
bool publishingComplete = false;
bool cycleInProgress = false;
bool retractConfirmed = true;
bool cycleStartPublished = false;
bool cycleEndPublished = false;

// sd pending for timing
String pendingSD[10];
int pendingCount = 0;

// timer
unsigned long lastDeploy = 0;
const unsigned long DEPLOY_INTERVAL = 3UL * 60UL * 1000UL;

// ---- FAILSAFE ADDITIONS ----
// These only activate when things go wrong — no effect on normal operation
const int     MAX_RETRIES            = 3;
const unsigned long DEPLOYED_TIMEOUT = 120000; // 2 min — covers full acoustic dump + deploy
const unsigned long RETRACTED_TIMEOUT = 30000; // 30 sec per retract attempt

int           deployRetryCount   = 0;
int           retractRetryCount  = 0;
bool          waitingForDeployed = false; // true after DEPLOY sent, before DEPLOYED received
bool          waitingForRetracted= false; // true after RETRACT sent, before RETRACTED received
bool          pendingRetract     = false; // retract was never confirmed — retry next cycle
unsigned long deployedSentAt     = 0;    // when DEPLOY was last sent
unsigned long retractedSentAt    = 0;    // when RETRACT was last sent
// ---- END FAILSAFE ADDITIONS ----

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

//testing function to simulate losing cell underwater
int cloudDisconnect(String arg) {
    Serial.println("Simulating underwater — killing cellular radio...");
    Cellular.off();
    return 1;
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

bool publishWithRetry(String payload) {
    for (int i = 0; i < 3; i++) {
        if (Particle.publish(PUBLISH_EVENT, payload, PRIVATE)) {
            return true;
        }
        Serial.println("Publish failed, retrying...");
        delay(1500);
    }
    return false;
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

    if (publishWithRetry(payload)) {
        Serial.println("Published: " + payload);
    } else {
        Serial.println("Failed to publish after 3 attempts: " + payload);
    }
    lastPublish = millis();
}

void publishNextFromSD() {
    if (!sdReady) return;

    SdFile f;
    if (!f.open(DATA_FILE, O_READ)) {
        if (allDataReceived && deployedConfirmed && !publishingComplete) {
            publishingComplete = true;
            Serial.println("No data to publish. Publishing complete.");
        }
        return;
    }

    uint32_t offset = readProgress();
    uint32_t fileSize = f.fileSize();

    if (offset >= fileSize) {
        f.close();
        if (allDataReceived && deployedConfirmed && !publishingComplete) {
            if (sd.remove(DATA_FILE)) Serial.println("Data file deleted.");
            if (sd.remove(PROGRESS_FILE)) Serial.println("Progress file deleted.");
            allDataReceived = false;
            publishingComplete = true;
            Serial.println("Publishing complete.");
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

int triggerDeploy(String arg) {
    retractSent = false;
    retractConfirmed = false;
    cycleStartPublished = false;
    cycleEndPublished = false;
    lastDeploy = millis();

    // Failsafe: reset deploy tracking
    deployRetryCount = 0;
    waitingForDeployed = true;
    deployedSentAt = millis();

    Serial.println("Sending DEPLOY...");
    Serial1.print("$B06DEPLOY");
    Serial.println("Waiting for data, DONE, and DEPLOYED...");
    return 1;
}

int retract(String arg) {
    Serial.println("Sending RETRACT...");
    Serial1.print("$B07RETRACT");
    retractSent = true;
    cycleInProgress = false;
    allDataReceived = false;
    deployedConfirmed = false;
    publishingComplete = false;
    lastDeploy = millis();

    // Failsafe: reset retract tracking
    retractRetryCount = 1;
    waitingForRetracted = true;
    retractedSentAt = millis();

    return 1;
}

void resetCycleFlags() {
    allDataReceived    = false;
    deployedConfirmed  = false;
    publishingComplete = false;
    cycleStartPublished= false;
    cycleEndPublished  = false;
    retractSent        = false;
    waitingForDeployed = false;
    waitingForRetracted= false;
    deployRetryCount   = 0;
    retractRetryCount  = 0;
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(9600);

    allDataReceived = false;
    deployedConfirmed = false;
    retractSent = false;
    publishingComplete = false;
    cycleInProgress = false;
    retractConfirmed = true;
    cycleStartPublished = false;
    cycleEndPublished = false;

    sdReady = initSD();
    if (sdReady) {
        Serial.println("SD ready.");
    }

    Particle.function("deploy", triggerDeploy);
    Particle.function("retract", retract);
    Particle.function("goUnderwater", cloudDisconnect);

    waitFor(Particle.connected, 30000);
    Serial.println("Ready. Call 'deploy' from console to begin.");
    lastDeploy = millis();
}

void loop() {
    // ---- FAILSAFE: retry pending RETRACT from previous cycle ----
    // If a retract was never confirmed last cycle, retry it before deploying again
    if (pendingRetract && millis() - lastDeploy >= DEPLOY_INTERVAL && retractConfirmed == false) {
        Serial.println("Failsafe: retrying pending RETRACT from previous cycle...");
        Serial1.print("$B07RETRACT");
        retractRetryCount = 1;
        waitingForRetracted = true;
        retractedSentAt = millis();
        pendingRetract = false;
        return;
    }

    // Auto-deploy on schedule — unchanged
    if (millis() - lastDeploy >= DEPLOY_INTERVAL && !cycleInProgress && retractConfirmed) {
        triggerDeploy("");
    }

    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 10000 && !cycleInProgress && retractConfirmed) {
        lastHeartbeat = millis();
        if (millis() - lastDeploy >= DEPLOY_INTERVAL) {
            Serial.println("Status: Deploy due!");
        } else {
            Serial.print("Status: Waiting... Time until next deploy: ");
            Serial.print((DEPLOY_INTERVAL - (millis() - lastDeploy)) / 1000);
            Serial.println("s");
        }
    }

    // Read incoming acoustic modem data — unchanged
    while (Serial1.available()) {
        char c = Serial1.read();

        if (c == '\n' || c == '\r') {
            serialBuffer.trim();
            if (serialBuffer.length() > 0) {
                Serial.println("Raw from modem: " + serialBuffer);

                if (serialBuffer.startsWith("#B") && serialBuffer.length() > 7) {
                    String data = serialBuffer.substring(7);

                    if (data == "DONE") {
                        Serial.println("All data received. Waiting for DEPLOYED...");
                        allDataReceived = true;
                    } else if (data == "DEPLOYED") {
                        Serial.println("Deployment confirmed! Starting publish...");
                        deployedConfirmed = true;
                        waitingForDeployed = false; // Failsafe: confirmed, stop watching
                    } else if (data == "RETRACTED") {
                        Serial.println("Retract confirmed.");
                        retractConfirmed = true;
                        waitingForRetracted = false; // Failsafe: confirmed, stop watching
                        retractRetryCount = 0;
                        pendingRetract = false;
                        lastDeploy = millis();
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

    // ---- FAILSAFE: DEPLOY timeout and retry ----
    // Only triggers if DEPLOYED was never received — happy path sets waitingForDeployed=false
    if (waitingForDeployed && millis() - deployedSentAt >= DEPLOYED_TIMEOUT) {
        if (deployRetryCount < MAX_RETRIES) {
            deployRetryCount++;
            Serial.print("Failsafe: No DEPLOYED received, retry ");
            Serial.print(deployRetryCount);
            Serial.print("/");
            Serial.println(MAX_RETRIES);
            Serial1.print("$B06DEPLOY");
            deployedSentAt = millis();
        } else {
            Serial.println("Failsafe: Max DEPLOY retries reached — sending RETRACT and going idle.");
            Serial1.print("$B07RETRACT");
            waitingForDeployed = false;
            retractRetryCount  = 1;
            waitingForRetracted= true;
            retractedSentAt    = millis();
            retractSent        = true;
            retractConfirmed   = false;
            resetCycleFlags();
            retractSent        = true;        // keep retractSent so idle waits for confirmation
            waitingForRetracted= true;
            retractedSentAt    = millis();
        }
    }

    // ---- FAILSAFE: RETRACT timeout and retry ----
    // Only triggers if RETRACTED was never received — happy path sets waitingForRetracted=false
    if (waitingForRetracted && millis() - retractedSentAt >= RETRACTED_TIMEOUT) {
        if (retractRetryCount < MAX_RETRIES) {
            retractRetryCount++;
            Serial.print("Failsafe: No RETRACTED received, retry ");
            Serial.print(retractRetryCount);
            Serial.print("/");
            Serial.println(MAX_RETRIES);
            Serial1.print("$B07RETRACT");
            retractedSentAt = millis();
        } else {
            Serial.println("Failsafe: Max RETRACT retries reached — going idle, will retry next cycle.");
            waitingForRetracted = false;
            retractRetryCount   = 0;
            pendingRetract      = true;  // flag to retry retract before next deploy
            retractConfirmed    = false; // keep false so auto-deploy won't fire
            lastDeploy          = millis();
        }
    }

    // Only publish once BOTH data received AND deployment confirmed — unchanged
    // Gap is 2000ms — safely within Particle's 1 publish/sec rate limit
    if ((allDataReceived || cycleStartPublished) && deployedConfirmed && millis() - lastPublish >= 2000) {

        if (!Particle.connected()) {
            Serial.println("Waiting for cell connection...");
            Cellular.on();
            if (!waitFor(Particle.connected, 600000)) {
                // Timed out — give up and retract
                Serial.println("Cell timeout — sending RETRACT and resetting.");
                Serial1.print("$B07RETRACT");
                retractSent         = true;
                retractRetryCount   = 1;
                waitingForRetracted = true;
                retractedSentAt     = millis();
                allDataReceived     = false;
                deployedConfirmed   = false;
                publishingComplete  = false;
                cycleStartPublished = false;
                cycleEndPublished   = false;
                retractConfirmed    = false;
                return;
            }
        }

        // Publish cycle_start FIRST before any data rows
        if (!cycleStartPublished) {
            if (Particle.publish("cycle_start", "", PRIVATE)) {
                Serial.println("cycle_start published.");
                cycleStartPublished = true;
            } else {
                Serial.println("cycle_start failed, retrying next tick.");
            }
            lastPublish = millis();
            return;
        }

        // Publish one data row per tick — gap enforced by millis() check above
        if (!publishingComplete) {
            publishNextFromSD();
            lastPublish = millis(); // ensure gap resets after every row
            return;
        }

        // Publish cycle_end after all rows — waits one full gap after last row
        if (!cycleEndPublished) {
            if (Particle.publish("cycle_end", "", PRIVATE)) {
                Serial.println("cycle_end published.");
                cycleEndPublished = true;
                lastPublish = millis();
            } else {
                Serial.println("cycle_end failed, retrying next tick.");
                lastPublish = millis(); // back off before retry
            }
            return;
        }
    }

    // Send RETRACT once cycle_end confirmed sent — unchanged trigger condition
    if (publishingComplete && cycleEndPublished && !retractSent) {
        Serial.println("Sending RETRACT...");
        Serial1.print("$B07RETRACT");
        retractSent         = true;
        cycleInProgress     = false;
        allDataReceived     = false;
        deployedConfirmed   = false;
        publishingComplete  = false;
        cycleStartPublished = false;
        cycleEndPublished   = false;

        // Failsafe: start watching for RETRACTED
        retractRetryCount   = 1;
        waitingForRetracted = true;
        retractedSentAt     = millis();
        retractConfirmed    = false;
    }
}
