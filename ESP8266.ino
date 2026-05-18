const int ecgPin = A0;
const int sampleInterval = 4; // 250Hz

void setup() {
    Serial.begin(115200);
    pinMode(ecgPin, INPUT);
}

void loop() {
    static unsigned long lastTime = 0;
    unsigned long now = millis();

    if (now - lastTime >= sampleInterval) {
        lastTime = now;
        int raw = analogRead(ecgPin);
        Serial.println(raw);
    }
}