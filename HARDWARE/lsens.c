#include "lsens.h"
#include "delay.h"

// --- 内部函数声明 ---
void Adc3_Init(void);
u16 Get_Adc3(u8 ch);

// 初始化光敏传感器
void Lsens_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    // 1. 使能时钟: GPIOF 和 ADC3
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOF | RCC_APB2Periph_ADC3, ENABLE);	
    
    // 2. 配置 PF8 为模拟输入 (战舰V3板载光敏接在PF8)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN; 
    GPIO_Init(GPIOF, &GPIO_InitStructure);	
    
    // 3. 初始化ADC3
    Adc3_Init();
}

// 读取光照强度 (0~100)
// 0: 最暗, 100: 最亮
u8 Lsens_Get_Val(void)
{
	u32 temp_val=0;
	u8 t;
	for(t=0; t<10; t++) // 读取10次
	{
		temp_val += Get_Adc3(ADC_Channel_6); // PF8对应ADC3通道6
		delay_ms(5);
	}
	temp_val /= 10; // 取平均值
	
    // 战舰V3原理图：光越亮 -> 电阻越小 -> 电压越高 -> ADC值越大
	if(temp_val > 4000) temp_val = 4000;
    
    // 归一化到 0-100
    // 直接除以40，这样 4000/40 = 100 (最亮)，0/40 = 0 (最暗)
    // 这符合原本注释中 "100,最亮" 的定义
	return (u8)(100-(temp_val / 40)); 
}

// --- 以下是ADC3底层驱动 (集成在这里，方便使用) ---

// 初始化ADC3
void Adc3_Init(void)
{
	ADC_InitTypeDef ADC_InitStructure;
	
    // 设置ADC分频因子6, 72M/6=12M, ADC最大不能超过14M
	RCC_ADCCLKConfig(RCC_PCLK2_Div6); 

	ADC_DeInit(ADC3);  // 复位ADC3
	
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;	// 独立模式
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;		// 单通道模式
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;	// 单次转换模式
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;	// 软件触发
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;	// 右对齐
	ADC_InitStructure.ADC_NbrOfChannel = 1;	// 1个通道
	ADC_Init(ADC3, &ADC_InitStructure);	// 初始化ADC3

	ADC_Cmd(ADC3, ENABLE);	// 使能ADC3

	ADC_ResetCalibration(ADC3);	// 复位校准
	while(ADC_GetResetCalibrationStatus(ADC3));	// 等待复位校准结束
	
	ADC_StartCalibration(ADC3);	 // 开启AD校准
	while(ADC_GetCalibrationStatus(ADC3));	 // 等待校准结束
}

// 获取ADC3通道值
u16 Get_Adc3(u8 ch)   
{
  	// 设置指定ADC的规则组通道，一个序列，采样时间
	ADC_RegularChannelConfig(ADC3, ch, 1, ADC_SampleTime_239Cycles5);	
	ADC_SoftwareStartConvCmd(ADC3, ENABLE);		// 使能指定的ADC3软件转换启动功能	
	while(!ADC_GetFlagStatus(ADC3, ADC_FLAG_EOC ));// 等待转换结束
	return ADC_GetConversionValue(ADC3);	// 返回最近一次ADC3规则组的转换结果
}
