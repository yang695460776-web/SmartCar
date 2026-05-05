#ifndef TASK_HPP
#define TASK_HPP

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

#include <HXCthread.hpp>
#include <MosControl.hpp>
#include <WebService.hpp>
#include <zw101.hpp>
#include <Buzzer.hpp>

// Pins
#define FP_TOUCHOUT 2
#define FP_CTRL 19

// Fingerprint module minimum power-on window (ms)
#define FINGERPRINT_POWER_TIME 8000

#define MFRC522_SCK   14
#define MFRC522_MISO  12
#define MFRC522_MOSI  13
#define MFRC522_SS    15
#define MFRC522_RST   -1

// Globals provided elsewhere
extern Buzzer buzzer;
extern MosControl mosCtrl;
extern byte authorizedCards[5][4];

// NFC reader instance (declared extern in WebService.hpp)
MFRC522 mfrc522(MFRC522_SS, MFRC522_RST);

static uint8_t fp_state = 0;

// Fingerprint identify thread:
// - On touch: open AP window immediately (for OTA/UI)
// - Power fingerprint module and attempt identify
// - On success: call onFingerprintMatched() which uses unified on/off toggle
HXC::thread<void> zw101_thread([]() {
    while (true) {
        if (digitalRead(FP_TOUCHOUT) == HIGH) {
            onFingerprintTouchDetected();

            const unsigned long powerStartMs = millis();
            digitalWrite(FP_CTRL, HIGH);
            delay(250); // warmup after power-on

            webLogPrintln("[FP] Touch detected, start identify...");

            bool verified = false;

            while (millis() - powerStartMs < FINGERPRINT_POWER_TIME) {
                // Send identify immediately; avoid spamming repeated commands.
                fp_state = zw101_PS_AutoIdentify(0x02, 0xFFFF);
                if (fp_state == 1) {
                    webLogPrintln("[FP] Identify OK -> action");
                    onFingerprintMatched();
                    buzzer.buzzSuccess();
                    verified = true;
                    break;
                }

                if (fp_state == 3) {
                    webLogPrintln("[FP] UART timeout, retry...");
                    delay(200); // small backoff before retry
                } else {
                    webLogPrintln("[FP] Not matched, retry...");
                    delay(200);
                }
            }

            digitalWrite(FP_CTRL, LOW);
            webLogPrintln("[FP] Power off");

            if (!verified) {
                webLogPrintln("[FP] Identify failed/timeout");
                buzzer.buzzError();
            }

            // Debounce and require touch release; keeps it responsive but avoids double-trigger.
            delay(80);
            while (digitalRead(FP_TOUCHOUT) == HIGH) {
                delay(20);
            }
            delay(80);
        } else {
            digitalWrite(FP_CTRL, LOW);
            delay(100);
        }
    }
});

// NFC scan thread (gated by isNfcScanActive(), but we currently keep NFC always on)
HXC::thread<void> MFRC522_thread([]() {
    webLogPrintln("[NFC] Thread started");
    while (true) {
        if (!isNfcScanActive()) {
            delay(100);
            continue;
        }

        if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
            delay(100);
            continue;
        }

        bool cardAuthorized = false;
        for (int i = 0; i < 5; i++) {
            if (mfrc522.uid.size == 4 && memcmp(mfrc522.uid.uidByte, authorizedCards[i], 4) == 0) {
                cardAuthorized = true;
                break;
            }
        }

        if (cardAuthorized) {
            webLogPrintln("[NFC] Authorized -> action");
            onMatched(); // unified on/off toggle
            buzzer.buzzSuccess();
        } else {
            webLogPrintln("[NFC] Unauthorized");
        }

        webLogPrint("[NFC] UID:");
        for (byte i = 0; i < mfrc522.uid.size; i++) {
            Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
            Serial.print(mfrc522.uid.uidByte[i], HEX);
        }
        Serial.println();

        mfrc522.PICC_HaltA();
        delay(1000);
    }
});

// Web thread: keeps OTA/UI reachable (AP window at boot, longer in maintenance mode)
HXC::thread<void> web_thread([]() {
    if (isMaintenanceMode()) {
        activateAccessWindow(MAINTENANCE_TIMEOUT_MS);
    } else {
        activateAccessWindow(BOOT_AP_WINDOW_MS);
    }
    esp32_web_create();
});

#endif
