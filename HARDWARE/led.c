#include "led.h"
#include "stm32f10x.h"

/**
  * @brief  初始化控制LED的GPIO引脚
  * @param  无
  * @retval 无
  */
void LED_Init(void)
{
	// 定义一个GPIO_InitTypeDef类型的结构体变量，用于存储GPIO的配置
	GPIO_InitTypeDef GPIO_InitStructure;
	
	// 使能GPIOB和GPIOE端口的时钟
	// 因为LED0接在PB5, LED1接在PE5
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE,ENABLE);
	
	/*----------配置LED0对应的引脚 PB5----------*/
	// 选择要操作的GPIO引脚
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
	// 设置引脚的最高输出速率为50MHz
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	// 设置引脚模式为推挽输出
	GPIO_InitStructure.GPIO_Mode =GPIO_Mode_Out_PP ;
	// 调用库函数，根据指定的参数初始化GPIOB
	GPIO_Init(GPIOB,&GPIO_InitStructure);
	
	/*----------配置LED1对应的引脚 PE5----------*/
	// 选择要操作的GPIO引脚 (这里重用了上面的结构体)
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
	// 设置引脚的最高输出速率为50MHz
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	// 设置引脚模式为推挽输出
	GPIO_InitStructure.GPIO_Mode =GPIO_Mode_Out_PP ;
	// 调用库函数，根据指定的参数初始化GPIOE
	GPIO_Init(GPIOE,&GPIO_InitStructure);
}
