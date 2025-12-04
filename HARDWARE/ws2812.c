#include "ws2812.h"
#include "delay.h"
// 使用最快的直接寄存器访问
#define PIN_HIGH()      (GPIOB->BSRR = GPIO_Pin_9)
#define PIN_LOW()       (GPIOB->BRR = GPIO_Pin_9)

//****************************************************************************//
//                          手动时序校准区域                                  //
//****************************************************************************//
// 这是我们用来调试的“旋钮”。
// T0H_CYCLES:  代表“0”码高电平的持续时间。数值越小，时间越短。
// T1H_CYCLES:  代表“1”码高电平的持续时间。数值越大，时间越长。
// 根据数据手册，T1H的时间必须明显长于T0H。

#define T0H_CYCLES 1  // <--- 请从这里开始修改
#define T1H_CYCLES 12 // <--- 请从这里开始修改

//****************************************************************************//

// 存储颜色数据的缓冲区
u8 RGB_Buffer[LED_NUM * 3];

static void WS2812_Send_Byte(u8 byte);

void WS2812_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    PIN_LOW();
}

static void WS2812_Send_Byte(u8 byte)
{
    u8 i;
    for(i=0; i<8; i++)
    {
        if(byte & 0x80) // 发送 "1"
        {
            volatile u8 cycles = T1H_CYCLES;
            PIN_HIGH();
            while(cycles--);
            PIN_LOW();
        }
        else // 发送 "0"
        {
            volatile u8 cycles = T0H_CYCLES;
            PIN_HIGH();
            while(cycles--);
            PIN_LOW();
        }
        byte <<= 1;
    }
}

void WS2812_Refresh(void)
{
    u16 i;
    __disable_irq();
    for(i=0; i < LED_NUM * 3; i++)
    {
        WS2812_Send_Byte(RGB_Buffer[i]);
    }
    __enable_irq();
    PIN_LOW();
    delay_us(60);
}


void WS2812_Set_Pixel_Color(u16 n, u8 r, u8 g, u8 b)
{
    if(n < LED_NUM)
    {
        RGB_Buffer[n*3 + 0] = g;
        RGB_Buffer[n*3 + 1] = r;
        RGB_Buffer[n*3 + 2] = b;
    }
}

void WS2812_Set_All_Color(u8 r, u8 g, u8 b)
{
    u16 i;
    for(i=0; i<LED_NUM; i++)
    {
        WS2812_Set_Pixel_Color(i, r, g, b);
    }
}

void WS2812_Clear(void)
{
    WS2812_Set_All_Color(0,0,0);
    WS2812_Refresh();
}


// 流水灯效果 (简单的从头到尾点亮)
void WS2812_FlowingLight(u8 r, u8 g, u8 b, u16 delay_ms_per_led)
{
    u16 i;
    for (i = 0; i < LED_NUM; i++)
    {
        WS2812_Set_All_Color(0, 0, 0); // 先熄灭所有
        WS2812_Set_Pixel_Color(i, r, g, b); // 点亮当前
        WS2812_Refresh();
        delay_ms(delay_ms_per_led);
    }
}

// 呼吸灯效果 (所有灯颜色渐变，这里简化为固定亮度显示)
void WS2812_BreathingLight(u8 r, u8 g, u8 b, u16 duration_ms)
{
    // 简化为直接显示，如果需要真正的渐变，需要更复杂的PWM或时序控制
    WS2812_Set_All_Color(r, g, b);
    WS2812_Refresh();
    delay_ms(duration_ms); // 持续显示一段时间
}

// 闪烁效果
void WS2812_Blink(u8 r, u8 g, u8 b, u16 on_ms, u16 off_ms, u8 count)
{
    u8 i;
    for (i = 0; i < count; i++)
    {
        WS2812_Set_All_Color(r, g, b);
        WS2812_Refresh();
        delay_ms(on_ms);
        WS2812_Clear();
        delay_ms(off_ms);
    }
}

// 爆闪 (用于紧急警报)
void WS2812_RapidFlash(u8 r, u8 g, u8 b, u16 duration_ms)
{
    // 此版本使用您工程中已有的 delay_ms 函数，完全兼容
    // 假设每次闪烁周期为100ms (亮50ms, 灭50ms)
    u32 iterations = duration_ms / 100; 
    u32 i;

    if (iterations == 0) iterations = 1; // 至少闪一次

    for(i = 0; i < iterations; i++)
    {
        WS2812_Set_All_Color(r, g, b);
        WS2812_Refresh();
        delay_ms(50); // 亮50ms
        WS2812_Clear();
        delay_ms(50); // 滅50ms
    }
}
