#include "stm32f10x.h"
#include "key.h"
#include "sys.h"
#include "delay.h"

/**
  * @brief  初始化按键相关的GPIO引脚
  * @param  无
  * @retval 无
  */
void KEY_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
	// 使能GPIOA和GPIOE端口的时钟
	// 因为按键WK_UP接在PA0, KEY0/1/2接在PE2/3/4
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA|RCC_APB2Periph_GPIOE,ENABLE);
	
	// 配置KEY0, KEY1, KEY2 (PE2, PE3, PE4)
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 此行对输入模式非必需，但设置也无妨
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // 设置为上拉输入模式，因为按键另一端接地
	GPIO_Init(GPIOE,&GPIO_InitStructure);
	
	// 单独配置WK_UP (PA0)
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD; // 设置为下拉输入模式，因为按键另一端接VCC
	GPIO_Init(GPIOA,&GPIO_InitStructure);
}

/**
  * @brief  扫描按键状态
  * @param  mode: 扫描模式
  * 0 - 不支持连续按 (按下后必须松开才能再次检测)
  * 1 - 支持连续按 (按住不放可持续检测)
  * @retval 按键值 (0: 无按键, 1: KEY0, 2: KEY1, 3: KEY2, 4: WK_UP)
  */
u8 KEY_Scan(u8 mode)
{
	// static变量，函数退出后其值保留，用于跟踪按键是否已松开
	static u8 key_up=1;
	
	// 如果支持连续按模式，则每次调用都重置松开标志
	if(mode)
	{
		key_up=1;
	}
	
	// 检查按键是否已松开，并且当前是否有按键按下
	// KEY0/1/2是低电平有效，WK_UP是高电平有效
	if(key_up&&(KEY0==0||KEY1==0||KEY2==0||WK_UP==1))
	{
		delay_ms(10); // 延时10ms，用于软件消抖
		key_up=0;     // 标记按键已被按下，防止在mode=0时连续触发
		
		// 再次检查哪个按键被按下，并返回对应的键值
		// 这里存在优先级：KEY0 > KEY1 > KEY2 > WK_UP
		if(KEY0==0)return KEY0_PRES;
		else if(KEY1==0)return KEY1_PRES;
		else if(KEY2==0)return KEY2_PRES;
		else if(WK_UP==1)return WKUP_PRES;
	}
	// 如果所有按键都已松开，则重置松开标志
	else if(KEY0==1&&KEY1==1&&KEY2==1&&WK_UP==0)
	{
		key_up=1;
	}
	
	return 0; // 没有按键按下，返回0
	
}
