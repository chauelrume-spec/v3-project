#ifndef __WS2812_H
#define __WS2812_H

#include "sys.h"

// Define the number of LEDs in your strip
#define LED_NUM 10

// Function Prototypes
void WS2812_Init(void);
void WS2812_Set_Pixel_Color(u16 n, u8 r, u8 g, u8 b);
void WS2812_Refresh(void);
void WS2812_Clear(void);
void WS2812_Set_All_Color(u8 r, u8 g, u8 b);
void WS2812_FlowingLight(u8 r, u8 g, u8 b, u16 delay_ms_per_led);
void WS2812_BreathingLight(u8 r, u8 g, u8 b, u16 duration_ms);
void WS2812_Blink(u8 r, u8 g, u8 b, u16 on_ms, u16 off_ms, u8 count);
void WS2812_RapidFlash(u8 r, u8 g, u8 b, u16 duration_ms);
#endif
