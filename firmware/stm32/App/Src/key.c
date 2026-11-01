/* ============================================================================
 *  key.c —— 按键扫描（PB12）
 *
 *  状态机：空闲 → 检测到低电平并稳定 20ms → 按下
 *          按住 1s              → 报 KEY_LONG（只报一次）
 *          松开并稳定 20ms      → 若没报过长按则报 KEY_SHORT
 * ============================================================================ */
#include "key.h"
#include "board.h"

#define KEY_DEBOUNCE_CNT   2      /* 10ms × 2 = 20ms 消抖 */
#define KEY_LONG_CNT       100    /* 10ms × 100 = 1s 长按 */

static uint8_t  pressed      = 0;  /* 当前稳定状态：1=按住 */
static uint8_t  longFired    = 0;  /* 本次按住期间是否已报过长按 */
static uint8_t  stableCnt    = 0;  /* 消抖计数 */
static uint16_t holdCnt      = 0;  /* 按住时长（10ms 单位） */

void key_init(void) {
    gpio_clock_enable(KEY_PORT);
    gpio_in_pullup(KEY_PORT, KEY_PIN);      /* 内部上拉：松开=高，按下=低 */
}

KeyEvent key_poll_10ms(void) {
    uint8_t raw = gpio_read(KEY_PORT, KEY_PIN);   /* 0 = 按下 */

    if (raw == 0) {
        /* ---- 按下方向 ---- */
        if (!pressed) {
            if (++stableCnt >= KEY_DEBOUNCE_CNT) {  /* 连续 20ms 为低 → 确认按下 */
                pressed = 1;
                longFired = 0;
                holdCnt = 0;
                stableCnt = 0;
            }
        } else if (holdCnt < 0xFFFF) {
            holdCnt++;
            if (!longFired && holdCnt >= KEY_LONG_CNT) {  /* 满 1s */
                longFired = 1;
                return KEY_LONG;
            }
        }
    } else {
        /* ---- 松开方向 ---- */
        if (pressed) {
            if (++stableCnt >= KEY_DEBOUNCE_CNT) {  /* 连续 20ms 为高 → 确认松开 */
                uint8_t wasLong = longFired;
                pressed = 0;
                stableCnt = 0;
                holdCnt = 0;
                if (!wasLong) return KEY_SHORT;      /* 没报过长按 → 短按 */
            }
        } else {
            stableCnt = 0;
        }
    }
    return KEY_NONE;
}
