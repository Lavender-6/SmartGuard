/* ============================================================================
 *  stm32f103_regs.h —— STM32F103xB 最小寄存器定义（不依赖 SPL / CMSIS 包）
 *
 *  只包含本工程用到的外设：RCC / GPIO / USART / Flash / SysTick / NVIC
 *  地址取自 RM0008，与 CMSIS 一致。需要新外设时按同样方式补即可。
 * ============================================================================ */
#ifndef STM32F103_REGS_H
#define STM32F103_REGS_H

#include <stdint.h>

/* ---------------- 基本类型别名 ---------------- */
typedef volatile uint32_t reg32;      /* 32 位读写寄存器 */
typedef volatile uint16_t reg16;

#define __IO        volatile
#define BIT(n)      (1UL << (n))

/* ---------------- 中断号（IRQn） ---------------- */
typedef enum {
    NonMaskableInt_IRQn = -14,
    HardFault_IRQn      = -13,
    SysTick_IRQn        = -1,
    WWDG_IRQn           = 0,
    PVD_IRQn            = 1,
    EXTI0_IRQn          = 6,
    EXTI1_IRQn          = 7,
    EXTI9_5_IRQn        = 23,
    EXTI15_10_IRQn      = 40,
    USART1_IRQn         = 37,
    USART2_IRQn         = 38,
    USART3_IRQn         = 39
} IRQn_Type;

/* ============================ RCC ============================ */
typedef struct {
    __IO uint32_t CR;          // 0x00 时钟控制
    __IO uint32_t CFGR;        // 0x04 时钟配置
    __IO uint32_t CIR;         // 0x08 时钟中断
    __IO uint32_t APB2RSTR;    // 0x0C
    __IO uint32_t APB1RSTR;    // 0x10
    __IO uint32_t AHBENR;      // 0x14 AHB 外设使能
    __IO uint32_t APB2ENR;     // 0x18 APB2 外设使能（GPIOA/B/C、AFIO、USART1）
    __IO uint32_t APB1ENR;     // 0x1C APB1 外设使能（USART2/3）
    __IO uint32_t BDCR;        // 0x20
    __IO uint32_t CSR;         // 0x24
} RCC_TypeDef;
#define RCC   ((RCC_TypeDef *)0x40021000UL)

/* RCC_CR 位 */
#define RCC_CR_HSION        BIT(0)
#define RCC_CR_HSIRDY       BIT(1)
#define RCC_CR_HSEON        BIT(16)
#define RCC_CR_HSERDY       BIT(17)
#define RCC_CR_PLLON        BIT(24)
#define RCC_CR_PLLRDY       BIT(25)

/* RCC_CFGR 位 / 字段 */
#define RCC_CFGR_SW              (3UL << 0)
#define RCC_CFGR_SW_HSI          (0UL << 0)
#define RCC_CFGR_SW_HSE          (1UL << 0)
#define RCC_CFGR_SW_PLL          (2UL << 0)
#define RCC_CFGR_SWS             (3UL << 2)
#define RCC_CFGR_SWS_PLL         (2UL << 2)
#define RCC_CFGR_HPRE_DIV1       (0UL << 4)
#define RCC_CFGR_PPRE1_DIV2      (4UL << 8)    /* APB1 最高 36MHz，必须 /2 */
#define RCC_CFGR_PPRE2_DIV1      (0UL << 11)
#define RCC_CFGR_PLLSRC_HSE      (1UL << 16)   /* PLL 输入选 HSE（8MHz） */
#define RCC_CFGR_PLLXTPRE_DIV1   (0UL << 17)
/* 8MHz × 9 = 72MHz */
#define RCC_CFGR_PLLMULL9        (7UL << 18)

/* APB2ENR 位 */
#define RCC_APB2ENR_AFIOEN   BIT(0)
#define RCC_APB2ENR_IOPAEN   BIT(2)
#define RCC_APB2ENR_IOPBEN   BIT(3)
#define RCC_APB2ENR_IOPCEN   BIT(4)
#define RCC_APB2ENR_USART1EN BIT(14)
/* APB1ENR 位 */
#define RCC_APB1ENR_USART2EN BIT(17)
#define RCC_APB1ENR_USART3EN BIT(18)

/* ============================ GPIO ============================
 * 每个端口 7 个寄存器 + 2 个 32 位配置寄存器（CRL 管 0~7 脚，CRH 管 8~15 脚）
 * 每脚 4 bit：CNF[1:0] MODE[1:0]
 *   输入：MODE=00，CNF=10 → 上拉/下拉
 *   输出：MODE=01(10MHz)/10(2MHz)/11(50MHz)，CNF=00 推挽 / 01 开漏
 *   复用推挽：CNF=10，MODE=11 → 50MHz
 * ============================================================== */
typedef struct {
    __IO uint32_t CRL;   // 0x00 端口配置低（Pin 0-7）
    __IO uint32_t CRH;   // 0x04 端口配置高（Pin 8-15）
    __IO uint32_t IDR;   // 0x08 输入数据
    __IO uint32_t ODR;   // 0x0C 输出数据
    __IO uint32_t BSRR;  // 0x10 位设置/清除（低 16 置位，高 16 复位）
    __IO uint32_t BRR;   // 0x14 位复位
    __IO uint32_t LCKR;  // 0x18 配置锁定
} GPIO_TypeDef;

#define GPIOA  ((GPIO_TypeDef *)0x40010800UL)
#define GPIOB  ((GPIO_TypeDef *)0x40010C00UL)
#define GPIOC  ((GPIO_TypeDef *)0x40011000UL)
#define GPIOD  ((GPIO_TypeDef *)0x40011400UL)

/* ============================ USART ============================ */
typedef struct {
    __IO uint32_t SR;    // 0x00 状态
    __IO uint32_t DR;    // 0x04 数据
    __IO uint32_t BRR;   // 0x08 波特率
    __IO uint32_t CR1;   // 0x0C 控制 1（UE/TE/RE/RXNEIE）
    __IO uint32_t CR2;   // 0x10 控制 2
    __IO uint32_t CR3;   // 0x14 控制 3
    __IO uint32_t GTPR;  // 0x18
} USART_TypeDef;

#define USART1 ((USART_TypeDef *)0x40013800UL)   /* PA9=TX  PA10=RX */
#define USART2 ((USART_TypeDef *)0x40004400UL)   /* PA2=TX  PA3=RXD */
#define USART3 ((USART_TypeDef *)0x40004800UL)   /* PB10=TX PB11=RX */

#define USART_SR_PE      BIT(0)
#define USART_SR_FE      BIT(1)
#define USART_SR_NE      BIT(2)
#define USART_SR_ORE     BIT(3)
#define USART_SR_RXNE    BIT(5)   /* 收到一个字节 */
#define USART_SR_TC      BIT(6)   /* 发送完成 */
#define USART_SR_TXE     BIT(7)   /* 发送寄存器空 */

#define USART_CR1_RE     BIT(2)
#define USART_CR1_TE     BIT(3)
#define USART_CR1_RXNEIE BIT(5)
#define USART_CR1_UE     BIT(13)

/* ============================ Flash ============================ */
typedef struct {
    __IO uint32_t ACR;    // 0x00 访问控制（等待周期 / 预取）
    __IO uint32_t KEYR;
    __IO uint32_t OPTKEYR;
    __IO uint32_t SR;
    __IO uint32_t CR;
    __IO uint32_t AR;
    __IO uint32_t RESERVED;
    __IO uint32_t OBR;
    __IO uint32_t WRPR;
} FLASH_TypeDef;
#define FLASH ((FLASH_TypeDef *)0x40022000UL)

#define FLASH_ACR_LATENCY_2  BIT(1)   /* 48MHz < HCLK ≤ 72MHz 需要 2 个等待周期 */
#define FLASH_ACR_PRFTBE     BIT(4)   /* 预取缓冲使能 */

/* ============================ SysTick ============================ */
#define SYST_CSR   (*(__IO uint32_t *)0xE000E010UL)
#define SYST_RVR   (*(__IO uint32_t *)0xE000E014UL)
#define SYST_CVR   (*(__IO uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE      BIT(0)
#define SYST_CSR_TICKINT     BIT(1)
#define SYST_CSR_CLKSOURCE   BIT(2)

/* ============================ NVIC ============================ */
#define NVIC_ISER   ((__IO uint32_t *)0xE000E100UL)   /* 使能 */
#define NVIC_ICER   ((__IO uint32_t *)0xE000E180UL)   /* 禁用 */
#define NVIC_IPR    ((__IO uint8_t  *)0xE400E400UL)   /* 优先级（简化地址） */

/** 使能某个中断：NVIC_EnableIRQ(USART1_IRQn) */
static inline void NVIC_EnableIRQ(IRQn_Type irq) {
    if (irq >= 0) {
        NVIC_ISER[(uint32_t)irq >> 5] = BIT((uint32_t)irq & 0x1F);
    }
}

/** 关中断 / 开中断（临界区） */
static inline void __disable_irq(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void __enable_irq(void)  { __asm volatile ("cpsie i" ::: "memory"); }

#endif /* STM32F103_REGS_H */
