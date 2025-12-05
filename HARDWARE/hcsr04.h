#ifndef __HCSR04_H
#define __HCSR04_H
#include "sys.h"

// 接口定义
#define HCSR04_TRIG  PBout(12) // 发送触发信号
#define HCSR04_ECHO  PBin(13)  // 接收回响信号

void HCSR04_Init(void);
u32 HCSR04_Get_Distance(void); // 返回单位: mm

#endif

