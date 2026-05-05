#include <Arduino.h>
#include <zw101.hpp>
#include <WiFi.h>
#include <WebServer.h>
#include <MosControl.hpp>
#include <HXCthread.hpp>
#include <WebService.hpp>
#include <task.hpp>
#include <Buzzer.hpp>

#define FP_TOUCHOUT 2
#define FP_CTRL 19
#define BUZZER_PIN 16

Buzzer buzzer(BUZZER_PIN);
MosControl mosCtrl(27, 26, 25);

void setup() {
  Serial.begin(115200);
  // Read maintenance flag early to choose boot path
  initMaintenanceMode();

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FP_TOUCHOUT, INPUT);
  pinMode(FP_CTRL, OUTPUT);

  buzzer.begin();
  buzzer.buzzSuccess();  // 上电提示音：确认主板已启动
  mosCtrl.begin();

  // Always start web thread so OTA/management page stays reachable
  web_thread.start("web_thread", 4096);

  // Fail-safe: even if a stale maintenance flag exists, clear it and continue normal boot.
  if (isMaintenanceMode()) {
    Serial.println("[BOOT] Stale maintenance flag detected, clearing and continuing normal startup.");
    writeMaintenanceFlag(false);
    initMaintenanceMode();
  }

  // Normal mode: init peripherals and start worker threads
  SPI.begin(MFRC522_SCK, MFRC522_MISO, MFRC522_MOSI, MFRC522_SS);
  mfrc522.PCD_Init();
  mySerial.begin(57600, SERIAL_8N1, 17, 18);

  zw101_thread.start("zw101_thread", 4096);
  MFRC522_thread.start("MFRC522_thread", 4096);

  Serial.println("[BOOT] Normal mode: all threads started.");
}

void loop() {
  // Non-blocking buzzer timing requires periodic update
  buzzer.update();
}
