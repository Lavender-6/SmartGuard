/* ============================================================================
 *  board.h —— 本工程硬件板级定义（引脚 / 时钟 / 延时接口）
 *
 *  【接线表】STM32F103C8T6（Blue Pill）最小系统板
 *  ┌───────────────┬──────────────┬────────────────────────────────────────┐
 *  │ 功能          │ STM32 引脚   │ 说明                                   │
 *  ├───────────────┼──────────────┼────────────────────────────────────────┤
 *  │ SWD CLK       │ PA14         │ → WCH-Link 的 CLK（时钟线）            │
 *  │ SWD DIO       │ PA13         │ → WCH-Link 的 DIO（数据线）            │
 *  │ GND           │ GND          │ → WCH-Link GND（必须共地！）           │
 *  │ 3.3V          │ 3V3          │ WCH-Link 的 3V3（可选，也可 USB 供电）  │
 *  ├───────────────┼──────────────┼────────────────────────────────────────┤
 *  │ 调试串口 TX   │ PA9  (USART1)│ → USB-TTL 的 RX（看 printf）           │
 *  │ 调试串口 RX   │ PA10 (USART1)│ → USB-TTL 的 TX                        │
 *  ├───────────────┼──────────────┼────────────────────────────────────────┤
 *  │ ESP8266 TX    │ PA3  (USART2)│ ← ESP8266 的 TXD（模块发→单片机收 PA3）│
 *  │ ESP8266 RX    │ PA2  (USART2)│ → ESP8266 的 RXD（单片机发→模块收 PA2）│
 *  │ ESP8266 CH_PD │ PB0          │ 高电平使能（模块上常标 CH_PD / EN）    │
 *  │ ESP8266 RST   │ PB1          │ 低电平复位（可不接，接上更可控）       │
 *  ├───────────────┼──────────────┼────────────────────────────────────────┤
 *  │ 按键 KEY1     │ PB12         │ 按下=低电平，内部上拉，20ms 消抖       │
 *  │ 板载 LED      │ PC13         │ Blue Pill 自带，低电平点亮（心跳用）   │
 *  └───────────────┴──────────────┴────────────────────────────────────────┘
 *
 *  ⚠️ 供电要点：
 *    · ESP8266 峰值电流约 300mA，**不要用 STM32 板上的 3.3V 稳压器供电**
 *      （Blue Pill 的 AMS1117 只给得起 ~100mA），请用独立 3.3V 电源，务必共地。
 *    · ESP8266 是 3.3V 器件，TX/RX 不要接 5V。
 *    · CH_PD 必须拉高，否则模块不工作（本工程用 PB0 软件拉高）。
 *
 *  烧录（WCH-Link，SWD 两线）：
 *    CLK → PA14，DIO → PA13，GND → GND，3V3 → 3V3
 *    OpenOCD 命令见 tools/flash.md；也可以在 CLion 里用 OpenOCD 调试配置。
 * ============================================================================ */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include "stm32f103_regs.h"
#include "system_stm32f103.h"

/* ---------------- 引脚定义（改这里就能换脚） ---------------- */
#define LED_PORT        GPIOC
#define LED_PIN         13          /* 板载 LED，低电平点亮 */

#define KEY_PORT        GPIOB
#define KEY_PIN         12          /* 按键，按下接地 */

#define ESP_EN_PORT     GPIOB        /* CH_PD / EN */
#define ESP_EN_PIN      0
#define ESP_RST_PORT    GPIOB        /* RST（低复位） */
#define ESP_RST_PIN     1

#define ESP_UART        USART2       /* PB 上的串口留给 ESP8266 */
#define DBG_UART        USART1       /* PA9/PA10 给 USB-TTL 打印 */

/* ---------------- GPIO 小工具 ---------------- */
/** 推挽输出，50MHz */
static inline void gpio_out_pp(GPIO_TypeDef *port, uint8_t pin) {
    uint32_t shift = (pin < 8) ? (pin * 4) : ((pin - 8) * 4);
    volatile uint32_t *cfg = (pin < 8) ? &port->CRL : &port->CRH;
    *cfg &= ~(0xFUL << shift);
    *cfg |= (0x3UL << shift);            /* MODE=11 输出 50MHz, CNF=00 推挽 */
}

/** 复用推挽输出，50MHz（USART TX） */
static inline void gpio_af_pp(GPIO_TypeDef *port, uint8_t pin) {
    uint32_t shift = (pin < 8) ? (pin * 4) : ((pin - 8) * 4);
    volatile uint32_t *cfg = (pin < 8) ? &port->CRL : &port->CRH;
    *cfg &= ~(0xFUL << shift);
    *cfg |= (0xBUL << shift);            /* MODE=11 输出 50MHz, CNF=10 复用推挽 */
}

/** 浮空输入（USART RX） */
static inline void gpio_in_float(GPIO_TypeDef *port, uint8_t pin) {
    uint32_t shift = (pin < 8) ? (pin * 4) : ((pin - 8) * 4);
    volatile uint32_t *cfg = (pin < 8) ? &port->CRL : &port->CRH;
    *cfg &= ~(0xFUL << shift);
    *cfg |= (0x4UL << shift);            /* MODE=00 输入, CNF=01 浮空 */
}

/** 上拉输入（按键）。端口 ODR 置 1 = 上拉，置 0 = 下拉 */
static inline void gpio_in_pullup(GPIO_TypeDef *port, uint8_t pin) {
    uint32_t shift = (pin < 8) ? (pin * 4) : ((pin - 8) * 4);
    volatile uint32_t *cfg = (pin < 8) ? &port->CRL : &port->CRH;
    port->BSRR = BIT(pin);               /* ODR=1 → 上拉 */
    *cfg &= ~(0xFUL << shift);
    *cfg |= (0x8UL << shift);            /* MODE=00 输入, CNF=10 上拉/下拉 */
}

/** 读引脚电平（0/1） */
static inline uint8_t gpio_read(GPIO_TypeDef *port, uint8_t pin) {
    return (port->IDR & BIT(pin)) ? 1 : 0;
}

/** 写引脚电平 */
static inline void gpio_write(GPIO_TypeDef *port, uint8_t pin, uint8_t level) {
    if (level) port->BSRR = BIT(pin);
    else       port->BSRR = BIT(pin) << 16;
}

/** 翻转引脚 */
static inline void gpio_toggle(GPIO_TypeDef *port, uint8_t pin) {
    if (port->ODR & BIT(pin)) port->BSRR = BIT(pin) << 16;
    else                      port->BSRR = BIT(pin);
}

/** 给 GPIO 端口开时钟（RCC_APB2ENR 里的 IOPxEN） */
static inline void gpio_clock_enable(GPIO_TypeDef *port) {
    if (port == GPIOA) RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    else if (port == GPIOB) RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    else if (port == GPIOC) RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    else if (port == GPIOD) RCC->APB2ENR |= BIT(5);
}

#endif /* BOARD_H */
