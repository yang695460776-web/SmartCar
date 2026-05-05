#pragma once
#include <Arduino.h>

class Buzzer {
private:
    uint8_t pin;
    uint32_t stopTime = 0;
    bool isPlaying = false;

public:
    Buzzer(uint8_t pin) : pin(pin) {}

    void begin() {
#ifdef ESP32
        ledcSetup(0, 5000, 8);
        ledcAttachPin(pin, 0);
#else
        pinMode(pin, OUTPUT);
#endif
        stop();
    }

    void update() {
        if (isPlaying && millis() >= stopTime) {
            stop();
        }
    }

    // Short beep for success
    void buzzSuccess() { if (!isPlaying) playTone(440, 100); }
    // Longer beep for error (can sound like "long beep" if triggered repeatedly)
    void buzzError() { if (!isPlaying) playTone(294, 500); }

    // Alternating alert beeps (not currently used)
    void buzzAlert() {
        static uint8_t alertCount = 0;
        static uint32_t lastTime = 0;
        if (millis() - lastTime > 200) {
            playTone((alertCount++ % 2) ? 440 : 587, 100);
            lastTime = millis();
        }
    }

    bool playTone(uint16_t freq, uint32_t duration) {
        if (isPlaying || duration == 0) return false;
#ifdef ESP32
        ledcWriteTone(0, freq);
#else
        tone(pin, freq);
#endif
        stopTime = millis() + duration;
        isPlaying = true;
        return true;
    }

    void stop() {
#ifdef ESP32
        ledcWrite(0, 0);
#else
        noTone(pin);
#endif
        isPlaying = false;
    }

    bool isBusy() const { return isPlaying; }
};

