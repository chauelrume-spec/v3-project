#include "hcsr04.h"
#include "delay.h"

void HCSR04_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    // TRIG: 推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    HCSR04_TRIG = 0;

    // ECHO: 下拉输入 (或浮空输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

u32 HCSR04_Get_Distance(void)
{
    u32 t = 0;
    
    // 1. 发送至少10us的高电平触发信号
    HCSR04_TRIG = 1;
    delay_us(20);
    HCSR04_TRIG = 0;

    // 2. 等待Echo变高 (超时处理防止卡死)
    while(HCSR04_ECHO == 0)
    {
        t++;
        delay_us(1);
        if(t > 5000) return 9999; // 超时
    }

    // 3. 测量Echo高电平时间
    t = 0;
    while(HCSR04_ECHO == 1)
    {
        t++;
        delay_us(9); // 校准值，粗略对应距离计算
        if(t > 5000) break; // 防止死循环
    }
    
    // 4. 计算距离: 距离 = 时间 * 速度 / 2
    // 这里的 t 大约对应 1.5mm (经验值，取决于delay精度)
    return t * 2; // 返回毫米 (近似值，用于防盗足够了)
}
