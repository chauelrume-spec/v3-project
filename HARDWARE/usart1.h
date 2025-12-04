#ifndef __USART_H
#define __USART_H
#include "stdio.h"
#include "sys.h"

#define USART_REC_LEN  		200  	//定义最大接收字节数 200 (Define max receive bytes 200)
//#define EN_USART1_RX 		1		//使能(1)/禁止(0) 串口1接收 (Enable(1)/Disable(0) USART1 receive)

extern u8 USART_RX_BUF[USART_REC_LEN];     	//接收缓冲,最大USART_REC_LEN个字节,末字节为换行符 (Receive buffer, max USART_REC_LEN bytes, last byte is newline)
//extern u16 USART_RX_STA;      		        //接收状态标记 (Receive status flag)

void uart_init(u32 bound);
#endif
