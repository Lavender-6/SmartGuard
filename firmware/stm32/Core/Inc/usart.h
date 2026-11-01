/* ============================================================================
 *  usart.h —— USART1（调试打印）+ USART2（ESP8266）驱动
 *
 *  · 轮询发送：printf 直接往 USART1->DR 写，不占用中断
 *  · 接收：USART2 用中断把字节塞进环形缓冲，App 层取
 * ============================================================================ */
#ifndef USART_H
#define USART_H

#include <stdint.h>
#include "board.h"

/* ---- 初始化 ---- */
void usart1_init(uint32_t baud);      /* PA9=TX / PA10=RX，调试口 */
void usart2_init(uint32_t baud);      /* PA2=TX / PA3=RX，接 ESP8266 */

/* ---- 发送 ---- */
void usart_putc(USART_TypeDef *u, char c);
void usart_write(USART_TypeDef *u, const char *s);
void usart_write_n(USART_TypeDef *u, const char *s, uint32_t len);

/* ---- 接收 ---- */
void     usart2_set_rx_callback(void (*cb)(uint8_t byte));  /* 每收到 1 字节回调（在中断里执行，要短） */
int      usart2_read_byte(void);      /* 有数据返回 0~255，无数据返回 -1 */
void     usart2_flush_rx(void);       /* 清空接收缓冲 */

/* ---- printf 重定向：printf("hello %d\r\n", 1) 会走 USART1 ---- */
int _write(int fd, char *ptr, int len);

#endif
