const int SERVO_PIN = 9;
const int PERIOD_US = 20000;

void sendPulses(int pulseUs, int ms) {
    // Send pulses continuously for `ms` milliseconds
    unsigned long start = millis();
    while (millis() - start < ms) {
        digitalWrite(SERVO_PIN, HIGH);
        delayMicroseconds(pulseUs);
        digitalWrite(SERVO_PIN, LOW);
        delayMicroseconds(PERIOD_US - pulseUs);
    }
}

void setup() {
    pinMode(SERVO_PIN, OUTPUT);
    digitalWrite(SERVO_PIN, LOW);

    sendPulses(800,  2000);  // full clockwise for 2 seconds
    sendPulses(1500, 2000);  // center for 2 seconds
    sendPulses(2200, 2000);  // full counter-clockwise for 2 seconds
    sendPulses(1500, 2000);  // center for 2 seconds
}

void loop() {}