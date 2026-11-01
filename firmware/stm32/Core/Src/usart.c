/* ============================================================================
 *  usart.c —— USART1 调试口 + USART2 接 ESP8266
 * ============================================================================ */
#include "usart.h"

/* ============================== 公共部分 ============================== */

static void usart_enable_clock(USART_TypeDef *u) {
    if (u == USART1)      RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    else if (u == USART2) RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    else if (u == USART3) RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
}

/**
 * 设置波特率。USART1 挂在 APB2（72MHz），USART2/3 挂在 APB1（36MHz）。
 * BRR 计算公式：整数部分 + 小数部分（4 bit），用定点 16 倍放大实现四舍五入。
 */
static void usart_set_baud(USART_TypeDef *u, uint32_t baud) {
    uint32_t pclk = (u == USART1) ? SystemCoreClock
                                  : (SystemCoreClock / 2);   /* APB1 = HCLK/2 */
    uint32_t div  = (pclk + baud / 2) / baud;                 /* 例如 72000000/115200 ≈ 625 */
    uint32_t mant = div / 16;
    uint32_t frac = div % 16;
    u->BRR = (mant << 4) | (frac & 0xF);
}

void usart_putc(USART_TypeDef *u, char c) {
    while (!(u->SR & USART_SR_TXE)) { }     /* 等发送寄存器空 */
    u->DR = (uint16_t)(uint8_t)c;
}

void usart_write_n(USART_TypeDef *u, const char *s, uint32_t len) {
    while (len--) usart_putc(u, *s++);
}

void usart_write(USART_TypeDef *u, const char *s) {
    while (*s) usart_putc(u, *s++);
}

/* ============================== USART1 ============================== */
void usart1_init(uint32_t baud) {
    gpio_clock_enable(GPIOA);
    gpio_af_pp(GPIOA, 9);        /* PA9  → TX */
    gpio_in_float(GPIOA, 10);    /* PA10 ← RX */

    usart_enable_clock(USART1);
    usart_set_baud(USART1, baud);
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;   /* 轮询模式，不开 RXNEIE */
}

/** 让 printf 走 USART1（newlib 的 _write 钩子） */
int _write(int fd, char *ptr, int len) {
    (void)fd;
    usart_write_n(USART1, ptr, (uint32_t)len);
    return len;
}

/* newlib 的 syscall 桩：没有文件系统，直接返回错误码，消除链接告警 */
int _close(int fd)            { (void)fd; return -1; }
int _lseek(int fd, int p, int w) { (void)fd; (void)p; (void)w; return 0; }
int _read(int fd, char *p, int len) { (void)fd; (void)p; (void)len; return 0; }
int _fstat(int fd, void *st)  { (void)fd; (void)st; return 0; }
int _isatty(int fd)           { (void)fd; return 1; }
int _kill(int pid, int sig)   { (void)pid; (void)sig; return -1; }
int _getpid(void)             { return 1; }

/* ============================== USART2 ==============================
 *  接 ESP8266：PA2(TX) → 模块 RXD，PA3(RX) ← 模块 TXD
 *  中断接收，环形缓冲 256 字节
 * ==================================================================== */
#define RX_BUF_SIZE 256

static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;      /* 写指针（中断里改） */
static volatile uint16_t rx_tail = 0;      /* 读指针（主循环里改） */
static void (*volatile rx_callback)(uint8_t) = 0;

void usart2_init(uint32_t baud) {
    gpio_clock_enable(GPIOA);
    gpio_af_pp(GPIOA, 2);        /* PA2  → TX（复用推挽） */
    gpio_in_float(GPIOA, 3);     /* PA3  ← RX（浮空输入） */

    usart_enable_clock(USART2);
    usart_set_baud(USART2, baud);
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    NVIC_EnableIRQ(USART2_IRQn);
}

void usart2_set_rx_callback(void (*cb)(uint8_t)) {
    rx_callback = cb;
}

int usart2_read_byte(void) {
    if (rx_head == rx_tail) return -1;
    uint8_t b = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1) % RX_BUF_SIZE);
    return b;
}

void usart2_flush_rx(void) {
    rx_tail = rx_head;
}

/** USART2 中断：收到 1 字节推进缓冲，满了丢最老的 */
void USART2_IRQHandler(void) {
    /* ORE(溢出) 也要进来读一次 DR 才能清除，否则中断会卡死 */
    if (USART2->SR & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t b = (uint8_t)(USART2->DR & 0xFF);     /* 读 DR 同时清 RXNE/ORE */

        uint16_t next = (uint16_t)((rx_head + 1) % RX_BUF_SIZE);
        if (next != rx_tail) {                        /* 不满才写 */
            rx_buf[rx_head] = b;
            rx_head = next;
        }

        void (*cb)(uint8_t) = rx_callback;            /* 拷贝指针再调用 */
        if (cb) cb(b);
    }
    /* 帧错误/噪声：由下一次读 DR 清除，无需额外处理 */
}
