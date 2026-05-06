#ifndef MOS_CONTROL_HPP
#define MOS_CONTROL_HPP

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class MosControl {
public:
    enum FunctionState {
        POWER_ON,
        LOCK,
        UNLOCK,
        FUNC_MAX
    };

    MosControl(int powerPin, int lockPin, int unlockPin)
        : _powerPin(powerPin),
          _lockPin(lockPin),
          _unlockPin(unlockPin),
          _isOn(false),
          _lastAction(LOCK),
          _lastOpMs(0),
          _opMutex(nullptr) {}

    // Your scooter wiring:
    // - POWER_ON pin simulates the "power" button (turn on)
    // - UNLOCK pin is used as "power off/shutdown"
    // - LOCK pin currently unused but kept for manual testing
    static constexpr bool USE_UNLOCK_PIN_FOR_OFF = true;
    // Remote-button press timing (ms). Tune if your remote expects longer/shorter press.
    static constexpr uint32_t BUTTON_PRESS_MS = 250;
    static constexpr uint32_t BUTTON_GAP_MS = 120;
    static constexpr uint32_t DOUBLE_PRESS_GAP_MS = 250;
    // Prevent rapid re-triggers (card left on reader, finger jitter, etc.)
    static constexpr uint32_t MIN_ACTION_INTERVAL_MS_NFC = 1800;
    static constexpr uint32_t MIN_ACTION_INTERVAL_MS_FP = 350;

    void begin() {
        pinMode(_powerPin, OUTPUT);
        pinMode(_lockPin, OUTPUT);
        pinMode(_unlockPin, OUTPUT);
        if (_opMutex == nullptr) {
            _opMutex = xSemaphoreCreateMutex();
        }
        allOff();
    }

    // Unified on/off toggle:
    // - If currently OFF/locked: do POWER_ON pulse
    // - If currently ON/unlocked: do OFF pulse (UNLOCK on your wiring)
    // NFC and fingerprint both call this.
    FunctionState toggleOnOffNfc() { return toggleOnOffWithMinInterval(MIN_ACTION_INTERVAL_MS_NFC); }
    FunctionState toggleOnOffFingerprint() { return toggleOnOffWithMinInterval(MIN_ACTION_INTERVAL_MS_FP); }

    // Default: keep old behavior for callers we haven't migrated yet.
    FunctionState toggleOnOff() { return toggleOnOffWithMinInterval(MIN_ACTION_INTERVAL_MS_NFC); }

private:
    FunctionState toggleOnOffWithMinInterval(uint32_t minIntervalMs) {
        lockOperation();

        const uint32_t now = millis();
        if ((uint32_t)(now - _lastOpMs) < minIntervalMs) {
            // Too soon: ignore this trigger to avoid open-close-open oscillation.
            unlockOperation();
            return _lastAction;
        }
        _lastOpMs = now;

        allOff();
        if (_isOn) {
            // Turn off/shutdown
            if (USE_UNLOCK_PIN_FOR_OFF) {
                pulseUnlock();
                _lastAction = UNLOCK;
            } else {
                // Fallback: many controllers toggle power on/off using the same "power button" pulse.
                pulsePowerOn();
                _lastAction = POWER_ON;
            }
            _isOn = false;
        } else {
            pulsePowerOn();
            _isOn = true;
            _lastAction = POWER_ON;
        }
        unlockOperation();
        return _lastAction;
    }

    bool isOn() const { return _isOn; }
    FunctionState lastAction() const { return _lastAction; }

public:
    // Manual action (used by Web UI buttons).
    void doAction(FunctionState action) {
        lockOperation();
        allOff();

        switch (action) {
            case POWER_ON:
                pulsePowerOn();
                _isOn = true;
                _lastAction = POWER_ON;
                break;
            case LOCK:
                pulseLock();
                _lastAction = LOCK;
                break;
            case UNLOCK:
                pulseUnlock();
                _isOn = false;
                _lastAction = UNLOCK;
                break;
            default:
                break;
        }

        unlockOperation();
    }

private:
    const int _powerPin;
    const int _lockPin;
    const int _unlockPin;
    bool _isOn;
    FunctionState _lastAction;
    uint32_t _lastOpMs;
    SemaphoreHandle_t _opMutex;

    void lockOperation() {
        if (_opMutex != nullptr) {
            xSemaphoreTake(_opMutex, portMAX_DELAY);
        }
    }

    void unlockOperation() {
        if (_opMutex != nullptr) {
            xSemaphoreGive(_opMutex);
        }
    }

    void allOff() {
        digitalWrite(_powerPin, LOW);
        digitalWrite(_lockPin, LOW);
        digitalWrite(_unlockPin, LOW);
    }

    void pressPin(int pin) {
        digitalWrite(pin, HIGH);
        delay(BUTTON_PRESS_MS);
        digitalWrite(pin, LOW);
        delay(BUTTON_GAP_MS);
    }

    void pulsePowerOn() {
        // Your remote requires a double-press for power.
        pressPin(_powerPin);
        delay(DOUBLE_PRESS_GAP_MS);
        pressPin(_powerPin);
    }

    void pulseLock() {
        pressPin(_lockPin);
    }

    void pulseUnlock() {
        pressPin(_unlockPin);
    }
};

#endif
