#ifndef __PMS7003_H
#define __PMS7003_H

#include "sys.h"

// 定义一个结构体来存放所有解析后的PM数据
typedef struct
{
    // --- 核心浓度数据 (单位: ug/m3) ---
    u16 pm1_0_std;      // PM1.0 浓度 (标准颗粒物)
    u16 pm2_5_std;      // PM2.5 浓度 (标准颗粒物)
    u16 pm10_std;       // PM10  浓度 (标准颗粒物)

    // --- 大气环境下浓度数据 (通常我们用上面的标准值) ---
    u16 pm1_0_atm;      // PM1.0 浓度 (大气环境下)
    u16 pm2_5_atm;      // PM2.5 浓度 (大气环境下)
    u16 pm10_atm;       // PM10  浓度 (大气环境下)

    // --- 颗粒物数量数据 (单位: 个/0.1L) ---
    // 这是实现“火灾预警”的超灵敏数据！
    u16 particles_0_3um;  // 直径 > 0.3um 的颗粒物数量
    u16 particles_0_5um;  // 直径 > 0.5um 的颗粒物数量
    u16 particles_1_0um;  // 直径 > 1.0um 的颗粒物数量
    u16 particles_2_5um;  // 直径 > 2.5um 的颗粒物数量
    u16 particles_5_0um;  // 直径 > 5.0um 的颗粒物数量
    u16 particles_10um;   // 直径 > 10um 的颗粒物数量

    volatile u8 is_new;     // 新数据标志位 (必须保留)

} PMS_Data_t;

// 函数声明
void PMS7003_Init(void);
PMS_Data_t PMS7003_Get_Data(void);

#endif
