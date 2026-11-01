/* ============================================================================
 *  system_stm32f103.c —— 时钟树与延时
 *
 *  目标：SYSCLK = 72MHz（Blue Pill 上的 8MHz 晶振 × 9）
 *        AHB = 72MHz, APB2 = 72MHz, APB1 = 36MHz
 *
 *  如果板子没有晶振 / 晶振不起振 → 自动回退到 HSI 8MHz（程序还能跑，只是慢）
 * ============================================================================ */
#include "system_stm32f103.h"
#include "stm32f103_regs.h"

uint32_t SystemCoreClock = 8000000U;   /* 上电默认 HSI 8MHz */

void SystemInit(void) {
    /* 1) 打开 HSE（外部 8MHz 晶振）并等待起振 */
    RCC->CR |= RCC_CR_HSEON;
    uint32_t timeout = 0x20000;
    while (!(RCC->CR & RCC_CR_HSERDY) && --timeout) { }

    if (timeout == 0) {
        /* 晶振没起来：保持 HSI 8MHz 运行，避免死等（串口波特率会不对，但能启动） */
        SystemCoreClock = 8000000U;
        return;
    }

    /* 2) Flash 等待周期：72MHz 需要 2WS，并开启预取 */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /* 3) 总线分频：AHB/1、APB2/1、APB1/2（APB1 上限 36MHz） */
    RCC->CFGR &= ~(RCC_CFGR_HPRE_DIV1 | (7UL << 8) | (7UL << 11));
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

    /* 4) PLL：HSE(8MHz) × 9 = 72MHz */
    RCC->CFGR &= ~((1UL << 16) | (1UL << 17) | (0xFUL << 18));
    RCC->CFGR |= RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLXTPRE_DIV1 | RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    timeout = 0x20000;
    while (!(RCC->CR & RCC_CR_PLLRDY) && --timeout) { }
    if (timeout == 0) { SystemCoreClock = 8000000U; return; }

    /* 5) 切换 SYSCLK → PLL，等待切换完成 */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    timeout = 0x20000;
    while (((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) && --timeout) { }

    SystemCoreClock = (timeout == 0) ? 8000000U : 72000000U;
}

/* ---------------------------------------------------------------------------
 *  延时：用 SysTick 做一次性倒计时，不产生中断，也不影响后续使用
 * ------------------------------------------------------------------------- */
static void systick_delay_ticks(uint32_t ticks) {
    SYST_CSR = 0;                                   /* 先停 */
    SYST_RVR = ticks - 1;                           /* 24 位重装值 */
    SYST_CVR = 0;                                   /* 清当前值 */
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE;/* 时钟 = HCLK，启动 */

    while (!(SYST_CSR & (1UL << 16))) { }           /* 等 COUNTFLAG 置位 */
    SYST_CSR = 0;                                   /* 停掉，交还给后续使用者 */
}

void delay_ms(uint32_t ms) {
    while (ms--) {
        systick_delay_ticks(SystemCoreClock / 1000U);   /* 72MHz → 72000 ticks/ms */
    }
}

void delay_us(uint32_t us) {
    /* 1us = SystemCoreClock/1000000 ticks；72MHz 时是 72，最小按 1 处理 */
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    if (ticks == 0) ticks = 1;
    while (ticks > 0xFFFFFFUL) {                /* 超过 24 位就分段 */
        systick_delay_ticks(0xFFFFFFUL);
        ticks -= 0xFFFFFFUL;
    }
    systick_delay_ticks(ticks);
}
