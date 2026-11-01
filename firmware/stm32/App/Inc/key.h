/* ============================================================================
 *  key.h —— 按键（PB12，按下为低，内部上拉）
 *  跑在 10ms 轮询里：20ms 消抖 + 上升沿短按 + 长按 1s
 * ============================================================================ */
#ifndef KEY_H
#define KEY_H

#include <stdint.h>

typedef enum {
    KEY_NONE = 0,     /* 无事件 */
    KEY_SHORT,        /* 短按（松开时触发） */
    KEY_LONG          /* 长按（持续按住 1s 时触发一次） */
} KeyEvent;

void     key_init(void);
KeyEvent key_poll_10ms(void);   /* 每 10ms 调用一次，返回本周期产生的事件 */

#endif
