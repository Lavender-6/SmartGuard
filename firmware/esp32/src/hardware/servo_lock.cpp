#include "servo_lock.h"
#include "config.h"

// ESP32-S3 的 LEDC 最大分辨率只有 14 位（SOC_LEDC_TIMER_BIT_WIDE_NUM=14），
// 超过 ledcSetup 直接静默失败、timer 不配置、PWM 无输出（串口只有一行 log_e）。
// 舵机用经典的 10 位：50Hz 下 duty 步进约 19.5us，SG90 无感，且全系 ESP32 兼容。
#define SERVO_PWM_BITS   10
#define SERVO_PWM_MAX    ((1u << SERVO_PWM_BITS) - 1)   // 1023
#define SERVO_PERIOD_US  20000.0f                       // 50Hz → 20ms 周期

void ServoLock::begin(uint8_t pin, uint8_t channel) {
    _channel = channel;
    uint32_t actualFreq = ledcSetup(_channel, 50, SERVO_PWM_BITS);  // 50Hz、10 位分辨率
    if (!actualFreq) {
        Serial.println("[servo] LEDC setup FAILED (check freq/bits)");
    }
    ledcAttachPin(pin, _channel);
    _inited = true;
    lock();
}

void ServoLock::writeAngle(int angle) {
    if (!_inited) return;
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    float us = 500.0f + angle * 2000.0f / 180.0f;      // 500~2500us
    uint32_t duty = (uint32_t)(us / SERVO_PERIOD_US * SERVO_PWM_MAX);
    ledcWrite(_channel, duty);
}

void ServoLock::lock()   { writeAngle(SERVO_LOCK_ANGLE); }
void ServoLock::unlock() { writeAngle(SERVO_UNLOCK_ANGLE); }
