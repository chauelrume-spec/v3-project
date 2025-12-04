#include "sys.h"
#include "usart1.h"
#include "stm32f10x.h"

// 加入以下代码,支持printf函数,而不需要选择use MicroLIB
#if 1
#pragma import(__use_no_semihosting)
// 标准库需要的支持函数 (Standard library required support function)

struct __FILE
{
    int handle;
};
FILE __stdout;
// 定义_sys_exit()以避免使用半主机模式 (Define _sys_exit() to avoid using semihosting mode)
void _sys_exit(int x)
{
    x = x;
}
// 重定义fputc函数 (Redefine fputc function)
int fputc(int ch, FILE *f)
{
    while((USART1->SR & 0X40) == 0); // 循环发送,直到发送完毕 (Loop send, until transmission is complete)
    USART1->DR = (u8) ch;
    return ch;
}
#endif

// 串口中断服务程序 (Serial port interrupt service routine)
// 注意:读取USARTx->SR能避免莫名其妙的错误 (Note: Reading USARTx->SR can avoid strange errors)
u8 USART_RX_BUF[USART_REC_LEN];     // 接收缓冲,最大USART_REC_LEN个字节。 (Receive buffer, max USART_REC_LEN bytes.)
// 接收状态 (Receive status)
// flag=0,接收未完成 (flag=0, receive incomplete)
// flag=1,检测到#,开始接收数据 (flag=1, # detected, start receiving data)
// flag=2,接收完毕 (flag=2, reception complete)
u8 flag=0;
u16 len = 0; // 数据长度 (Data length)

void uart_init(u32 bound)
{
    // GPIO口设置 (GPIO pin settings)
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 使能USART1,GPIOA时钟 (Enable USART1, GPIOA clock)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1|RCC_APB2Periph_GPIOA, ENABLE);

    // USART1_TX  GPIOA.9
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; // PA.9
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出 (Multiplexed push-pull output)
    GPIO_Init(GPIOA, &GPIO_InitStructure); // 初始化GPIOA.9 (Initialize GPIOA.9)

    // USART1_RX	GPIOA.10初始化 (USART1_RX GPIOA.10 initialization)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10; // PA.10
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // 浮空输入 (Floating input)
    GPIO_Init(GPIOA, &GPIO_InitStructure); // 初始化GPIOA.10 (Initialize GPIOA.10)

    // Usart1 NVIC 配置 (Usart1 NVIC configuration)
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3; // 抢占优先级3 (Preemption priority 3)
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3; // 子优先级3 (Sub priority 3)
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; // IRQ通道使能 (IRQ channel enable)
    NVIC_Init(&NVIC_InitStructure); // 根据指定的参数初始化NVIC寄存器 (Initialize NVIC register according to specified parameters)

    // USART 初始化设置 (USART initialization settings)
    USART_InitStructure.USART_BaudRate = bound; // 串口波特率 (Serial port baud rate)
    USART_InitStructure.USART_WordLength = USART_WordLength_8b; // 字长为8位数据格式 (Word length is 8-bit data format)
    USART_InitStructure.USART_StopBits = USART_StopBits_1; // 一个停止位 (One stop bit)
    USART_InitStructure.USART_Parity = USART_Parity_No; // 无奇偶校验位 (No parity bit)
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // 无硬件数据流控制 (No hardware flow control)
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; // 收发模式 (Receive and transmit mode)
    USART_Init(USART1, &USART_InitStructure); // 初始化串口 (Initialize serial port)
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE); // 开启串口接受中断 (Enable serial port receive interrupt)
    USART_Cmd(USART1, ENABLE); // 使能串口1 (Enable USART1)
}

void USART1_IRQHandler(void)
{
    u8 Res;
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) // 接收中断 (Receive interrupt)
    {
        Res = USART_ReceiveData(USART1); // 读取接收到的数据 (Read the received data)

        if (flag == 0 && Res == 0x23) // 未接收并且检测到以#打头的数据 (Not yet received and detected data starting with #)
        {
            flag = 1;
            return;
        }

        if (flag == 1)
        {
            if (Res == 0x2A)
            {
                flag = 2; // 接收完毕 (Reception complete)
            }
            else
            {
                if (len < USART_REC_LEN)
                {
                    USART_RX_BUF[len] = Res;
                    len++;
                }
                else
                {
                    len = 0;
                }
            }
        }
    }
}

