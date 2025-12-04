#include "led.h"
#include "delay.h"
#include "key.h"
#include "sys.h"
#include "lcd.h"
#include "usart.h"
#include "beep.h"
#include "dht11.h"
#include "pms7003.h"
#include "24cxx.h"
#include "ws2812.h"
#include <stdio.h>
#include "ai_model.h" // <-- 【已添加】 包含新的AI模型头文件

//-------------------- 全局变量和定义 --------------------//

// 定义EEPROM地址，方便管理
#define EEPROM_ADDR_TEMP_H  0x01
#define EEPROM_ADDR_TEMP_L  0x02
#define EEPROM_ADDR_HUMI_H  0x03
#define EEPROM_ADDR_HUMI_L  0x04
#define EEPROM_ADDR_PM25_H  0x05
#define EEPROM_ADDR_MAGIC   0x00 // 用于判断是否首次上电的标志位地址
#define EEPROM_MAGIC_NUM    0xAA // 首次上电写入的数字

// --- 系统状态变量 ---
typedef enum {
    PARAM_TEMP_H,
    PARAM_TEMP_L,
    PARAM_HUMI_H,
    PARAM_HUMI_L,
    PARAM_PM25_H
} SettingParam_t;

static u8 g_is_setting_mode = 0; // 0:正常显示模式, 1:参数设置模式
static SettingParam_t g_current_param = PARAM_TEMP_H; // 当前正在编辑的参数

// --- 阈值变量 ---
static u16 temp_H = 30;  // 温度上限阈值 (默认值)
static u16 temp_L = 10;  // 温度下限阈值 (默认值)
static u16 humi_H = 80;  // 湿度上限阈值 (默认值)
static u16 humi_L = 40;  // 湿度下限阈值 (默认值)
static u16 pm25_H = 75;  // PM2.5上限阈值 (默认值)

// --- 报警状态标志 ---
static u8 g_temp_alarm = 0;
static u8 g_humi_alarm = 0;
static u8 g_pm_alarm = 0;
static u8 g_ai_alarm = 0; // <-- 【已添加】 AI异常报警标志

//-------------------- 函数声明 --------------------//
void Load_Thresholds_From_EEPROM(void);
void UI_Display_Main(u8 temp, u8 humi, u16 pm1_0, u16 pm2_5, u16 pm10);
void Key_Process(void);
void Alarm_Check(u8 temp, u8 humi, u16 pm2_5);
void Alarm_Update(void);

//-------------------- 主函数 --------------------//
int main(void)
{
    u8 temperature = 0, humidity = 0;
    PMS_Data_t current_pm;
    
    // --- 1. 系统初始化 ---
    delay_init();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    uart_init(115200);
    LED_Init();
    LCD_Init();
    KEY_Init();
    BEEP_Init();
    AT24CXX_Init();
    PMS7003_Init();
    WS2812_Init();
    ai_model_init(); // <-- 【已添加】 初始化AI模型
    
    while(DHT11_Init()) {
        LCD_ShowString(30, 130, 200, 16, 16, (u8*)"DHT11 Error");
        delay_ms(500);
    }
    
    // --- 2. 从EEPROM加载阈值 ---
    Load_Thresholds_From_EEPROM();

    // --- 3. 绘制静态UI ---
    LCD_Clear(WHITE);
    POINT_COLOR = BLACK;
    LCD_ShowString(30, 20, 210, 16, 16, (u8*)"Warehouse Safety System");
    // 您的姓名学号
    LCD_ShowString(30, 290, 210, 16, 16, (u8*)"Name: XXX  ID: XXXXXX");

    // --- 4. 主循环 ---
    while(1)
    {
        // 读取传感器数据
        DHT11_Read_Data(&temperature, &humidity);
        current_pm = PMS7003_Get_Data();

        // 处理按键输入
        Key_Process();
        
        // 更新LCD显示
        if(current_pm.is_new) { // 只在有新PM数据时才刷新，节省资源
             UI_Display_Main(temperature, humidity, 
                             current_pm.pm1_0_std, current_pm.pm2_5_std, current_pm.pm10_std);
             
             // ---!!! AI集成代码开始 !!!---
             // 1. 准备浮点数组
             float ai_input_data[4]; 
             ai_input_data[0] = (float)temperature;
             ai_input_data[1] = (float)humidity;
             ai_input_data[2] = (float)current_pm.particles_0_3um;
             ai_input_data[3] = (float)current_pm.particles_2_5um;
             
             // 2. 调用我们自己的C函数进行预测
             int8_t anomaly_result = ai_model_predict(ai_input_data); 
             
             // 3. 根据AI结果设置报警标志 (注意：-1是异常)
             if (anomaly_result == -1) { // -1代表异常
                 g_ai_alarm = 1;
             } else { // 1代表正常
                 g_ai_alarm = 0;
             }
             // ---!!! AI集成代码结束 !!!---
        }

        // 检查报警条件
        Alarm_Check(temperature, humidity, current_pm.pm2_5_std);
        
        // 更新报警执行器 (蜂鸣器和WS2812)
        Alarm_Update();
        
        delay_ms(200); // 主循环周期
    }
}

//-------------------- 辅助函数 --------------------//

/**
 * @brief  从EEPROM加载阈值，如果首次上电则写入默认值
 */
void Load_Thresholds_From_EEPROM(void)
{
    u8 magic = AT24CXX_ReadOneByte(EEPROM_ADDR_MAGIC);
    
    if (magic == EEPROM_MAGIC_NUM) { // 不是首次上电，加载保存的值
        temp_H = AT24CXX_ReadOneByte(EEPROM_ADDR_TEMP_H);
        temp_L = AT24CXX_ReadOneByte(EEPROM_ADDR_TEMP_L);
        humi_H = AT24CXX_ReadOneByte(EEPROM_ADDR_HUMI_H);
        humi_L = AT24CXX_ReadOneByte(EEPROM_ADDR_HUMI_L);
        pm25_H = AT24CXX_ReadOneByte(EEPROM_ADDR_PM25_H);
    } else { // 首次上电，写入默认值
        AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L);
        AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L);
        AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_NUM); // 写入标志
    }
}

/**
 * @brief  刷新主界面UI显示
 */
void UI_Display_Main(u8 temp, u8 humi, u16 pm1_0, u16 pm2_5, u16 pm10)
{
    char text_buffer[40];

    // --- 显示传感器实时数据 ---
    POINT_COLOR = BLACK;
    sprintf(text_buffer, "Temp: %d C ", temp);
    LCD_ShowString(30, 50, 200, 16, 16, (u8*)text_buffer);
    sprintf(text_buffer, "Humi: %d %% ", humi);
    LCD_ShowString(30, 70, 200, 16, 16, (u8*)text_buffer);
    sprintf(text_buffer, "PM2.5: %d ug/m3 ", pm2_5);
    LCD_ShowString(30, 90, 210, 16, 16, (u8*)text_buffer);

    // --- 显示阈值 ---
    LCD_ShowString(30, 130, 200, 16, 16, (u8*)"-- Thresholds --");
    
    // 根据是否在设置模式，高亮显示当前编辑的参数
    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_TEMP_H) ? RED : BLUE;
    sprintf(text_buffer, "Temp H: %d C ", temp_H);
    LCD_ShowString(30, 150, 200, 16, 16, (u8*)text_buffer);

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_TEMP_L) ? RED : BLUE;
    sprintf(text_buffer, "Temp L: %d C ", temp_L);
    LCD_ShowString(30, 170, 200, 16, 16, (u8*)text_buffer);
    
    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_HUMI_H) ? RED : BLUE;
    sprintf(text_buffer, "Humi H: %d %% ", humi_H);
    LCD_ShowString(30, 190, 200, 16, 16, (u8*)text_buffer);

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_HUMI_L) ? RED : BLUE;
    sprintf(text_buffer, "Humi L: %d %% ", humi_L);
    LCD_ShowString(30, 210, 200, 16, 16, (u8*)text_buffer);

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_PM25_H) ? RED : BLUE;
    sprintf(text_buffer, "PM2.5 H: %d ug/m3 ", pm25_H);
    LCD_ShowString(30, 230, 210, 16, 16, (u8*)text_buffer);

    POINT_COLOR = BLACK; // 恢复默认颜色
}

/**
 * @brief  处理按键输入逻辑
 */
void Key_Process(void)
{
    u8 key = KEY_Scan(0);
    
    if (key == 1) { // KEY0: 切换模式/参数
        g_is_setting_mode = 1; // 进入设置模式
        g_current_param = (SettingParam_t)((g_current_param + 1) % 5);
    } else if (key == 2) { // KEY1: 增加
        if (g_is_setting_mode) {
            switch(g_current_param) {
                case PARAM_TEMP_H: if(temp_H<99) temp_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H); break;
                case PARAM_TEMP_L: if(temp_L<temp_H) temp_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L); break;
                case PARAM_HUMI_H: if(humi_H<100) humi_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H); break;
                case PARAM_HUMI_L: if(humi_L<humi_H) humi_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L); break;
                case PARAM_PM25_H: if(pm25_H<255) pm25_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H); break;
            }
        }
    } else if (key == 3) { // WK_UP (KEY2): 减少
        if (g_is_setting_mode) {
             switch(g_current_param) {
                case PARAM_TEMP_H: if(temp_H>temp_L) temp_H--; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H); break;
                case PARAM_TEMP_L: if(temp_L>0) temp_L--; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L); break;
                case PARAM_HUMI_H: if(humi_H>humi_L) humi_H--; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H); break;
                case PARAM_HUMI_L: if(humi_L>0) humi_L--; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L); break;
                case PARAM_PM25_H: if(pm25_H>0) pm25_H--; AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H); break;
            }
        }
    }
}

/**
 * @brief  检查传感器数据是否触发报警
 */
void Alarm_Check(u8 temp, u8 humi, u16 pm2_5)
{
    g_temp_alarm = (temp > temp_H || temp < temp_L);
    g_humi_alarm = (humi > humi_H || humi < humi_L);
    g_pm_alarm   = (pm2_5 > pm25_H);
}

/**
 * @brief  根据报警标志更新蜂鸣器和WS2812
 */
void Alarm_Update(void)
{
    static u16 tick = 0;
    tick++;

    if (g_is_setting_mode) { // 设置模式，不变
        BEEP = 0;
        if(tick % 2 == 0) { 
            WS2812_Set_All_Color(0,0,0);
            WS2812_Set_Pixel_Color( (tick/2) % LED_NUM, 0, 50, 50);
            WS2812_Refresh();
        }
        return;
    }

    // ---!!! 【已修改】AI报警逻辑 (最高优先级) !!!---
    if (g_ai_alarm) { 
        if (tick % 1 == 0) { // 最快速的爆闪
            BEEP = !BEEP; // 最急促的蜂鸣
            WS2812_Set_All_Color(255, 0, 0); // 刺眼的红色
            WS2812_Refresh();
        } else {
            WS2812_Clear();
        }
    } 
    // ---!!! AI报警逻辑结束 !!!---

    // 2. 固定阈值PM报警 (次高优先级)
    else if (g_pm_alarm) { 
        if (tick % 2 == 0) {
            BEEP = !BEEP;
            WS2812_Set_All_Color(200, 60, 0); // 橙色
            WS2812_Refresh();
        } else {
            WS2812_Clear();
        }
    } 
    // 3. 固定阈值温度报警
    else if (g_temp_alarm) {
        if (tick % 5 == 0) {
            BEEP = !BEEP;
            WS2812_Set_All_Color(200, 100, 0); // 黄色
            WS2812_Refresh();
        } else if (tick % 5 == 1) {
            WS2812_Clear();
        }
    } 
    // 4. 固定阈值湿度报警
    else if (g_humi_alarm) {
        if (tick % 10 == 0) {
            BEEP = !BEEP;
            WS2812_Set_All_Color(0, 0, 200); // 蓝色
            WS2812_Refresh();
        } else if (tick % 10 == 1) {
            WS2812_Clear();
        }
    } 
    // 5. 一切正常
    else { 
        BEEP = 0;
        WS2812_Set_All_Color(0, 50, 0); // 绿色常亮
        WS2812_Refresh();
    }
}
