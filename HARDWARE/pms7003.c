#include "pms7003.h"
#include "usart.h" // 假设你的printf重定向在这个文件里
#include "led.h"
// --- 全局变量 ---
// 串口接收缓冲区
static u8 g_rx_buffer[32];
// 接收状态机变量
static u8 g_rx_state = 0;
// 接收计数器
static u8 g_rx_count = 0;
// PM数据结构体实例
static PMS_Data_t g_pms_data = {0, 0, 0, 0};


// 内部函数声明
static void Parse_PMS_Data(void);

// 函数：初始化USART2和相关NVIC
// 参考Gitee例程：Project/USART/USART_Interrupt/usart.c
void PMS7003_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    // 2. 配置GPIO (PA2->TX, PA3->RX)
    // PA2 - USART2_TX
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    // PA3 - USART2_RX
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. 配置USART2
    USART_InitStructure.USART_BaudRate = 9600;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    // 4. 配置NVIC (使能USART2中断)
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 5. 使能USART2接收中断
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    // 6. 使能USART2
    USART_Cmd(USART2, ENABLE);
}


// 函数：串口2中断服务函数
// 这是驱动的核心，当硬件收到一个字节时，会自动调用这个函数
void USART2_IRQHandler(void)
{
	  LED1 = !LED1;
    u8 received_byte;
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        received_byte = USART_ReceiveData(USART2);

        // 状态机解析数据帧
        switch(g_rx_state)
        {
            case 0: // 等待帧头 0x42
                if(received_byte == 0x42)
                {
                    g_rx_buffer[g_rx_count++] = received_byte;
                    g_rx_state = 1;
                }
                break;

            case 1: // 等待帧头 0x4d
                if(received_byte == 0x4d)
                {
                    g_rx_buffer[g_rx_count++] = received_byte;
                    g_rx_state = 2;
                }
                else
                {
                    g_rx_state = 0;
                    g_rx_count = 0;
                }
                break;

            case 2: // 接收数据帧剩余部分
                g_rx_buffer[g_rx_count++] = received_byte;
                if(g_rx_count >= 32) // 接收满32个字节
                {
                    Parse_PMS_Data(); // 调用解析函数
                    g_rx_state = 0;
                    g_rx_count = 0;
                }
                break;

            default:
                g_rx_state = 0;
                g_rx_count = 0;
                break;
        }
    }
}


// 函数：解析一帧完整的数据
// 参考你之前找到的文件 (pm2.5st32).c
static void Parse_PMS_Data(void)
{
    u16 checksum_calc = 0;
    u16 checksum_recv = 0;
    int i;

    // 1. 计算校验和
    for(i=0; i<30; i++)
    {
        checksum_calc += g_rx_buffer[i];
    }
    checksum_recv = (g_rx_buffer[30] << 8) | g_rx_buffer[31];

    // 2. 校验和对比
    if(checksum_calc == checksum_recv)
    {
        // 校验成功，提取所有数据
        // Standard Particle (CF=1)
        g_pms_data.pm1_0_std = (g_rx_buffer[4] << 8) | g_rx_buffer[5];
        g_pms_data.pm2_5_std = (g_rx_buffer[6] << 8) | g_rx_buffer[7];
        g_pms_data.pm10_std  = (g_rx_buffer[8] << 8) | g_rx_buffer[9];

        // Atmospheric Environment
        g_pms_data.pm1_0_atm = (g_rx_buffer[10] << 8) | g_rx_buffer[11];
        g_pms_data.pm2_5_atm = (g_rx_buffer[12] << 8) | g_rx_buffer[13];
        g_pms_data.pm10_atm  = (g_rx_buffer[14] << 8) | g_rx_buffer[15];
        
        // Particle Count per 0.1L of air
        g_pms_data.particles_0_3um = (g_rx_buffer[16] << 8) | g_rx_buffer[17];
        g_pms_data.particles_0_5um = (g_rx_buffer[18] << 8) | g_rx_buffer[19];
        g_pms_data.particles_1_0um = (g_rx_buffer[20] << 8) | g_rx_buffer[21];
        g_pms_data.particles_2_5um = (g_rx_buffer[22] << 8) | g_rx_buffer[23];
        g_pms_data.particles_5_0um = (g_rx_buffer[24] << 8) | g_rx_buffer[25];
        g_pms_data.particles_10um  = (g_rx_buffer[26] << 8) | g_rx_buffer[27];
        
        g_pms_data.is_new = 1; // 设置新数据标志位
    }
}

// 函数：供外部调用的数据获取接口
PMS_Data_t PMS7003_Get_Data(void)
{
    // 先把当前的全局数据完整地复制一份到临时变量中
    PMS_Data_t temp_data_to_return = g_pms_data;

    // 检查全局数据中的标志位，如果为1，则将其清零，为下一次接收做准备
    if(g_pms_data.is_new)
    {
        g_pms_data.is_new = 0;
    }
    return temp_data_to_return;
}
