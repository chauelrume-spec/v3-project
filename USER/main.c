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
#include "ai_model.h"
#include "hcsr04.h"  // 【新增】超声波驱动
#include "lsens.h"   // 【新增】光敏驱动
#include <stdio.h>

//-------------------- 全局变量和定义 --------------------//

// --- EEPROM地址定义 ---
#define EEPROM_ADDR_TEMP_H  0x01
#define EEPROM_ADDR_TEMP_L  0x02
#define EEPROM_ADDR_HUMI_H  0x03
#define EEPROM_ADDR_HUMI_L  0x04
#define EEPROM_ADDR_PM25_H  0x05
#define EEPROM_ADDR_MAGIC   0x00 // 首次上电标志位
#define EEPROM_MAGIC_NUM    0xAA 

// --- 系统状态枚举 ---
typedef enum {
    PARAM_TEMP_H,
    PARAM_TEMP_L,
    PARAM_HUMI_H,
    PARAM_HUMI_L,
    PARAM_PM25_H
} SettingParam_t;

static u8 g_is_setting_mode = 0; // 0:监控模式, 1:设置模式
static SettingParam_t g_current_param = PARAM_TEMP_H; // 当前设置项

// --- 阈值变量 (从EEPROM加载) ---
static u16 temp_H = 30;  
static u16 temp_L = 10;  
static u16 humi_H = 80;  
static u16 humi_L = 40;  
static u16 pm25_H = 75;  

// --- 报警状态标志 ---
static u8 g_temp_alarm = 0;     // 温度报警
static u8 g_humi_alarm = 0;     // 湿度报警
static u8 g_pm_alarm = 0;       // PM浓度报警
static u8 g_ai_alarm = 0;       // AI火灾预警 (最高级)
static u8 g_security_alarm = 0; // 【新增】安防入侵报警 (次高级)

//-------------------- 函数声明 --------------------//
void Load_Thresholds_From_EEPROM(void);
void UI_Display_Main(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light);
void Key_Process(void);
void Alarm_Check_Env(u8 temp, u8 humi, u16 pm2_5);
void Alarm_Update(void);

//-------------------- 主函数 --------------------//
int main(void)
{
    u8 temperature = 0, humidity = 0;
    PMS_Data_t current_pm;
    u32 distance_mm = 0;
    u8 light_val = 0;
    
    // --- 1. 硬件初始化 ---
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
    ai_model_init(); // 初始化AI模型
    
    // 【新增】安防传感器初始化
    HCSR04_Init(); 
    Lsens_Init();  
    
    // 初始化DHT11 (带错误提示)
    while(DHT11_Init()) {
        LCD_ShowString(30, 130, 200, 16, 16, (u8*)"DHT11 Error");
        delay_ms(500);
    }
    
    // --- 2. 加载配置 ---
    Load_Thresholds_From_EEPROM();

    // --- 3. 绘制静态UI框架 ---
    LCD_Clear(WHITE);
    POINT_COLOR = BLACK;
    LCD_ShowString(30, 10, 210, 16, 16, (u8*)"Warehouse Safety System");
    LCD_ShowString(30, 300, 210, 16, 16, (u8*)"Design by: 23001040215"); // 您的署名

    // --- 4. 主循环 ---
    while(1)
    {
        // ========================
        // A. 数据采集 (Data Acquisition)
        // ========================
        DHT11_Read_Data(&temperature, &humidity);
        current_pm = PMS7003_Get_Data();
        distance_mm = HCSR04_Get_Distance(); // 读取距离
        light_val = Lsens_Get_Val();         // 读取光强(0-100)

        // ========================
        // B. 逻辑处理 (Logic Processing)
        // ========================
        
        // 1. 物理按键处理
        Key_Process();
        
        // 2. 安防逻辑判断 (非法入侵检测)
        // 逻辑: 距离<50cm(有人靠近) 或者 光强>20(夜间出现异常亮光)
        if (distance_mm < 500 || light_val > 100) {
            g_security_alarm = 1;
        } else {
            g_security_alarm = 0;
        }

        // 3. AI 火灾预警逻辑 (仅在有新PM数据时运行)
        if(current_pm.is_new) { 
             // 准备AI输入向量: [温度, 湿度, 0.3um颗粒数, 2.5um颗粒数]
             float ai_input_data[4]; 
             ai_input_data[0] = (float)temperature;
             ai_input_data[1] = (float)humidity;
             ai_input_data[2] = (float)current_pm.particles_0_3um;
             ai_input_data[3] = (float)current_pm.particles_2_5um;
             
             // 执行AI推理
             int8_t anomaly_result = ai_model_predict(ai_input_data); 
             
             // 设定AI报警标志 (-1代表异常)
             if (anomaly_result == -1) g_ai_alarm = 1;
             else g_ai_alarm = 0;
        }

        // 4. 常规环境阈值检查
        Alarm_Check_Env(temperature, humidity, current_pm.pm2_5_std);
        
        // ========================
        // C. 界面与执行 (UI & Actuators)
        // ========================
        
        // 刷新LCD显示
        UI_Display_Main(temperature, humidity, current_pm.pm2_5_std, distance_mm, light_val);
        
        // 执行声光报警 (蜂鸣器+WS2812)
        Alarm_Update();
        
        delay_ms(100); // 循环延时
    }
}

//-------------------- 辅助函数实现 --------------------//

// 从EEPROM加载阈值
void Load_Thresholds_From_EEPROM(void)
{
    u8 magic = AT24CXX_ReadOneByte(EEPROM_ADDR_MAGIC);
    
    if (magic == EEPROM_MAGIC_NUM) { 
        temp_H = AT24CXX_ReadOneByte(EEPROM_ADDR_TEMP_H);
        temp_L = AT24CXX_ReadOneByte(EEPROM_ADDR_TEMP_L);
        humi_H = AT24CXX_ReadOneByte(EEPROM_ADDR_HUMI_H);
        humi_L = AT24CXX_ReadOneByte(EEPROM_ADDR_HUMI_L);
        pm25_H = AT24CXX_ReadOneByte(EEPROM_ADDR_PM25_H);
    } else { 
        // 首次写入默认值
        AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L);
        AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L);
        AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_NUM); 
    }
}

// 刷新LCD界面
void UI_Display_Main(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light)
{
    char text_buffer[40];

    // 1. 环境数据区
    POINT_COLOR = BLACK;
    sprintf(text_buffer, "Temp: %d C   ", temp);
    LCD_ShowString(30, 40, 200, 16, 16, (u8*)text_buffer);
    sprintf(text_buffer, "Humi: %d %%   ", humi);
    LCD_ShowString(30, 60, 200, 16, 16, (u8*)text_buffer);
    sprintf(text_buffer, "PM2.5: %d ug/m3  ", pm2_5);
    LCD_ShowString(30, 80, 210, 16, 16, (u8*)text_buffer);
    
    // 2. 安防数据区 【新增】
    sprintf(text_buffer, "Dist: %d mm   ", dist);
    LCD_ShowString(30, 100, 200, 16, 16, (u8*)text_buffer);
    sprintf(text_buffer, "Light: %d %%   ", light);
    LCD_ShowString(30, 120, 200, 16, 16, (u8*)text_buffer);

    // 3. AI状态指示
    if(g_ai_alarm) {
        POINT_COLOR = RED;
        LCD_ShowString(30, 150, 210, 16, 16, (u8*)"[AI ALERT: FIRE!]   ");
    } else {
        POINT_COLOR = GREEN;
        LCD_ShowString(30, 150, 210, 16, 16, (u8*)"[AI System: Normal] ");
    }

    // 4. 阈值设置显示 (选中项高亮)
    POINT_COLOR = BLACK;
    LCD_ShowString(30, 180, 200, 16, 16, (u8*)"-- Settings --");
    
    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_TEMP_H) ? RED : BLUE;
    sprintf(text_buffer, "Temp H: %d ", temp_H);
    LCD_ShowString(30, 200, 100, 16, 16, (u8*)text_buffer);

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_TEMP_L) ? RED : BLUE;
    sprintf(text_buffer, "Temp L: %d ", temp_L);
    LCD_ShowString(130, 200, 100, 16, 16, (u8*)text_buffer);
    
    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_HUMI_H) ? RED : BLUE;
    sprintf(text_buffer, "Humi H: %d ", humi_H);
    LCD_ShowString(30, 220, 100, 16, 16, (u8*)text_buffer);

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_HUMI_L) ? RED : BLUE;
    sprintf(text_buffer, "Humi L: %d ", humi_L);
    LCD_ShowString(130, 220, 100, 16, 16, (u8*)text_buffer);

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_PM25_H) ? RED : BLUE;
    sprintf(text_buffer, "PM2.5 H: %d ", pm25_H);
    LCD_ShowString(30, 240, 200, 16, 16, (u8*)text_buffer);

    POINT_COLOR = BLACK; 
}

// 按键处理逻辑
void Key_Process(void)
{
    u8 key = KEY_Scan(0);
    
    if (key == KEY0_PRES) { // KEY0: 切换/进入设置
        g_is_setting_mode = 1; 
        g_current_param = (SettingParam_t)((g_current_param + 1) % 5);
    } else if (key == KEY1_PRES) { // KEY1: 增加
        if (g_is_setting_mode) {
            switch(g_current_param) {
                case PARAM_TEMP_H: if(temp_H<99) temp_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H); break;
                case PARAM_TEMP_L: if(temp_L<temp_H) temp_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L); break;
                case PARAM_HUMI_H: if(humi_H<100) humi_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H); break;
                case PARAM_HUMI_L: if(humi_L<humi_H) humi_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L); break;
                case PARAM_PM25_H: if(pm25_H<255) pm25_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H); break;
            }
        }
    } else if (key == WKUP_PRES) { // WK_UP: 减少
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

// 检查环境阈值
void Alarm_Check_Env(u8 temp, u8 humi, u16 pm2_5)
{
    g_temp_alarm = (temp > temp_H || temp < temp_L);
    g_humi_alarm = (humi > humi_H || humi < humi_L);
    g_pm_alarm   = (pm2_5 > pm25_H);
}

// 更新报警执行器 (包含静音逻辑)
void Alarm_Update(void)
{
    static u16 tick = 0;
    tick++;

    // ---------------------------------------------
    // 【图书馆模式】强制静音：始终关闭蜂鸣器
    // ---------------------------------------------
    BEEP = 0; 

    // 设置模式下：青色呼吸灯提示
    if (g_is_setting_mode) { 
        if(tick % 2 == 0) { 
            WS2812_Set_All_Color(0,0,0);
            WS2812_Set_Pixel_Color( (tick/2) % LED_NUM, 0, 50, 50);
            WS2812_Refresh();
        }
        return;
    }

    // 报警优先级判断：AI > 安防 > PM > 温度 > 湿度
    
    // 1. AI 火灾预警 (红色爆闪)
    if (g_ai_alarm) { 
        if (tick % 1 == 0) { // 极快闪烁
            // BEEP = !BEEP; // (已注释：静音)
            WS2812_Set_All_Color(255, 0, 0); 
            WS2812_Refresh();
        } else {
            WS2812_Clear();
        }
    } 
    // 2. 【新增】安防入侵报警 (紫色闪烁)
    else if (g_security_alarm) {
        if (tick % 5 == 0) { // 中速闪烁
            // BEEP = !BEEP; // (已注释：静音)
            WS2812_Set_All_Color(128, 0, 128); // 紫色 (RGB: 128,0,128)
            WS2812_Refresh();
        } else if (tick % 5 == 1) {
            WS2812_Clear();
        }
    }
    // 3. PM浓度超标 (橙色)
    else if (g_pm_alarm) { 
        if (tick % 2 == 0) {
            // BEEP = !BEEP; 
            WS2812_Set_All_Color(200, 60, 0); 
            WS2812_Refresh();
        } else {
            WS2812_Clear();
        }
    } 
    // 4. 温度超标 (黄色)
    else if (g_temp_alarm) {
        if (tick % 5 == 0) {
            // BEEP = !BEEP; 
            WS2812_Set_All_Color(200, 100, 0); 
            WS2812_Refresh();
        } else if (tick % 5 == 1) {
            WS2812_Clear();
        }
    } 
    // 5. 湿度超标 (蓝色)
    else if (g_humi_alarm) {
        if (tick % 10 == 0) {
            // BEEP = !BEEP; 
            WS2812_Set_All_Color(0, 0, 200); 
            WS2812_Refresh();
        } else if (tick % 10 == 1) {
            WS2812_Clear();
        }
    } 
    // 6. 一切正常 (绿色常亮)
    else { 
        // BEEP = 0; 
        WS2812_Set_All_Color(0, 50, 0); 
        WS2812_Refresh();
    }
}
