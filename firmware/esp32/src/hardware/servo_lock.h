#pragma once
#include <Arduino.h>

// ============================================================
//  舵机电子锁（ESP32-S3 LEDC PWM，50Hz）
//  0° = 关锁，90° = 开锁
// ============================================================
class ServoLock {
public:
    void begin(uint8_t pin, uint8_t channel);
    void lock();
    void unlock();
    void writeAngle(int angle);

private:
    uint8_t _channel;
    bool _inited = false;
};
