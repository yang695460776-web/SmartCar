#ifndef zw101_hpp
#define zw101_hpp

#include <Arduino.h>
#include <HardwareSerial.h>
#include <HXCthread.hpp>

extern HardwareSerial mySerial;

uint8_t zw101_GetEcho();
uint8_t zw101_PS_HandShake();
uint16_t zw101_checksum(uint8_t *packet, size_t len);
uint8_t zw101_sendpack(uint8_t *packet, size_t len);
uint8_t zw101_receivepack();
uint8_t zw101_ControlBLN(uint8_t featuer_code, uint8_t start_color, uint8_t end_color, uint8_t cycle_time);
uint8_t zw101_waitForHandshake(uint32_t timeout);
uint32_t zw101_PS_AutoEnroll(uint16_t ID, uint8_t entry_num, uint16_t parameter);
uint8_t zw101_PS_AutoIdentify(uint8_t rating_fraction, uint16_t ID);

HardwareSerial mySerial(2);

uint16_t zw101_checksum(uint8_t *packet, size_t len) {
    uint16_t checksum = 0;
    for (size_t i = 6; i < len - 2; i++) {
        checksum += packet[i];
    }
    return checksum;
}

uint8_t zw101_sendpack(uint8_t *packet, size_t len) {
    // 清理残留接收数据，避免上一帧影响本次解析
    while (mySerial.available() > 0) {
        mySerial.read();
    }

    size_t sentBytes = 0;
    for (size_t i = 0; i < len; i++) {
        if (mySerial.write(packet[i]) == 1) {
            sentBytes++;
        } else {
            return 0;
        }
    }
    mySerial.flush();
    return (sentBytes == len) ? 1 : 0;
}

uint8_t zw101_receivepack() {
    uint8_t packet[96];
    const uint32_t waitTimeoutMs = 1500;
    uint32_t startMs = millis();

    // 同步包头 0xEF 0x01
    int b = -1;
    do {
        if (millis() - startMs >= waitTimeoutMs) {
            return 3; // 超时
        }
        if (mySerial.available() > 0) {
            b = mySerial.read();
        } else {
            delay(2);
        }
    } while (b != 0xEF);
    packet[0] = static_cast<uint8_t>(b);

    while (mySerial.available() == 0) {
        if (millis() - startMs >= waitTimeoutMs) {
            return 3;
        }
        delay(2);
    }
    packet[1] = static_cast<uint8_t>(mySerial.read());
    if (packet[1] != 0x01) {
        return 2; // 非法包头
    }

    // 读取 addr(4) + pid(1) + len(2)
    for (int i = 2; i <= 8; i++) {
        while (mySerial.available() == 0) {
            if (millis() - startMs >= waitTimeoutMs) {
                return 3;
            }
            delay(2);
        }
        packet[i] = static_cast<uint8_t>(mySerial.read());
    }

    const uint16_t payloadLen = static_cast<uint16_t>((packet[7] << 8) | packet[8]);
    if (payloadLen < 2 || (9U + payloadLen) > sizeof(packet)) {
        return 2; // 长度异常
    }

    // 读取后续 payloadLen 字节（含确认码/数据/校验）
    for (uint16_t i = 0; i < payloadLen; i++) {
        while (mySerial.available() == 0) {
            if (millis() - startMs >= waitTimeoutMs) {
                return 3;
            }
            delay(2);
        }
        packet[9 + i] = static_cast<uint8_t>(mySerial.read());
    }

    // 应答包确认码通常位于 packet[9]
    const uint8_t confirmCode = packet[9];
    if (confirmCode == 0x00) {
        return 1; // 成功
    }
    if (confirmCode == 0x01) {
        return 0; // 明确失败
    }
    return 2;     // 其他错误码
}

uint8_t zw101_PS_HandShake() {
    mySerial.begin(57600, SERIAL_8N1, 17, 18);

    uint8_t packet[] = {
        0xEF, 0x01,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x01,
        0x00, 0x03,
        0x35,
        0x00, 0x39
    };

    uint16_t checksum = zw101_checksum(packet, sizeof(packet));
    packet[sizeof(packet) - 2] = static_cast<uint8_t>((checksum >> 8) & 0xFF);
    packet[sizeof(packet) - 1] = static_cast<uint8_t>(checksum & 0xFF);

    zw101_sendpack(packet, sizeof(packet));
    return zw101_receivepack();
}

uint8_t zw101_ControlBLN(uint8_t featuer_code, uint8_t start_color, uint8_t end_color, uint8_t cycle_time) {
    uint8_t packet[] = {
        0xEF, 0x01,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x01,
        0x00, 0x07,
        0x3C,
        featuer_code,
        start_color, end_color,
        cycle_time,
        0x00, 0x00
    };

    uint16_t checksum = zw101_checksum(packet, sizeof(packet));
    packet[sizeof(packet) - 2] = static_cast<uint8_t>((checksum >> 8) & 0xFF);
    packet[sizeof(packet) - 1] = static_cast<uint8_t>(checksum & 0xFF);

    zw101_sendpack(packet, sizeof(packet));
    return zw101_receivepack();
}

uint32_t zw101_PS_AutoEnroll(uint16_t ID, uint8_t entry_num, uint16_t parameter) {
    uint8_t packet[] = {
        0xEF, 0x01,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x01,
        0x00, 0x08,
        0x31,
        static_cast<uint8_t>((ID >> 8) & 0xFF), static_cast<uint8_t>(ID & 0xFF),
        entry_num,
        static_cast<uint8_t>((parameter >> 8) & 0xFF), static_cast<uint8_t>(parameter & 0xFF),
        0x00, 0x00
    };

    uint16_t checksum = zw101_checksum(packet, sizeof(packet));
    packet[sizeof(packet) - 2] = static_cast<uint8_t>((checksum >> 8) & 0xFF);
    packet[sizeof(packet) - 1] = static_cast<uint8_t>(checksum & 0xFF);

    zw101_sendpack(packet, sizeof(packet));
    return zw101_receivepack();
}

uint8_t zw101_PS_AutoIdentify(uint8_t rating_fraction, uint16_t ID) {
    uint8_t packet[] = {
        0xEF, 0x01,
        0xFF, 0xFF, 0xFF, 0xFF,
        0x01,
        0x00, 0x08,
        0x32,
        rating_fraction,
        static_cast<uint8_t>((ID >> 8) & 0xFF), static_cast<uint8_t>(ID & 0xFF),
        0x00, 0x05,
        0x00, 0x00
    };

    uint16_t checksum = zw101_checksum(packet, sizeof(packet));
    packet[sizeof(packet) - 2] = static_cast<uint8_t>((checksum >> 8) & 0xFF);
    packet[sizeof(packet) - 1] = static_cast<uint8_t>(checksum & 0xFF);

    zw101_sendpack(packet, sizeof(packet));
    return zw101_receivepack();
}

#endif
