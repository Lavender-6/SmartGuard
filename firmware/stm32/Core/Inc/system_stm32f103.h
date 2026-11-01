/* ============================================================================
 *  system_stm32f103.h —— 系统时钟初始化
 * ============================================================================ */
#ifndef SYSTEM_STM32F103_H
#define SYSTEM_STM32F103_H

#include <stdint.h>

/** 当前系统主频（Hz）。上电默认 HSI 8MHz，调用 SystemInit() 后变 72MHz */
extern uint32_t SystemCoreClock;

/** 由启动文件在 main() 之前调用：HSE 8MHz → PLL×9 → SYSCLK 72MHz */
void SystemInit(void);

/** 毫秒/微秒延时（基于 SysTick 轮询，不占用中断） */
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);

#endif
