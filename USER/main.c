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
#include "hcsr04.h"
#include "lsens.h"
#include <stdio.h>
#include "sdio_sdcard.h"
#include "malloc.h"
#include "ff.h"  
#include "exfuns.h"
#include "piclib.h"

//-------------------- 全局变量和定义 --------------------//

// EEPROM地址
#define EEPROM_ADDR_TEMP_H  0x01
#define EEPROM_ADDR_TEMP_L  0x02
#define EEPROM_ADDR_HUMI_H  0x03
#define EEPROM_ADDR_HUMI_L  0x04
#define EEPROM_ADDR_PM25_H  0x05
#define EEPROM_ADDR_MAGIC   0x00 
#define EEPROM_MAGIC_NUM    0xAA 

// --- 系统设置枚举 ---
typedef enum {
    PARAM_TEMP_H = 0,
    PARAM_TEMP_L,
    PARAM_HUMI_H,
    PARAM_HUMI_L,
    PARAM_PM25_H,
    PARAM_EXIT // 【新增】退出状态
} SettingParam_t;

// --- 【新增】灯光工作模式枚举 ---
typedef enum {
    MODE_AUTO = 0,      // 自动/正常模式 (绿灯待机，报警闪烁)
    MODE_EMERGENCY,     // 应急模式 (全白高亮，用于照明)
    MODE_OFF            // 关闭模式 (待机熄灭，报警仍闪烁)
} LightMode_t;

static u8 g_is_setting_mode = 0; 
static SettingParam_t g_current_param = PARAM_TEMP_H;
static LightMode_t g_light_mode = MODE_AUTO; // 默认为自动模式

// --- 阈值变量 ---
static u16 temp_H = 30;  
static u16 temp_L = 10;  
static u16 humi_H = 80;  
static u16 humi_L = 40;  
static u16 pm25_H = 75;  

// --- 报警状态标志 ---
static u8 g_temp_alarm = 0;
static u8 g_humi_alarm = 0;
static u8 g_pm_alarm = 0;
static u8 g_ai_alarm = 0;       
static u8 g_security_alarm = 0; 

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
    
    // --- 硬件初始化 ---
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
    ai_model_init();
    HCSR04_Init(); 
    Lsens_Init();  
    
    while(DHT11_Init()) {
        LCD_ShowString(30, 130, 200, 16, 16, (u8*)"DHT11 Error");
        delay_ms(500);
    }
    
    Load_Thresholds_From_EEPROM();

    LCD_Clear(WHITE);
    POINT_COLOR = BLACK;
    LCD_ShowString(30, 10, 210, 16, 16, (u8*)"Warehouse Safety System");
    LCD_ShowString(30, 300, 210, 16, 16, (u8*)"Design by: yqy");

    // --- 主循环 ---
    while(1)
    {
        // A. 数据采集
        DHT11_Read_Data(&temperature, &humidity);
        current_pm = PMS7003_Get_Data();
        distance_mm = HCSR04_Get_Distance(); 
        light_val = Lsens_Get_Val();         

        // B. 逻辑处理
        Key_Process(); // 处理按键 (模式切换/阈值设置)
        
        // 安防逻辑
        if (distance_mm < 500 || light_val > 200) g_security_alarm = 1;
        else g_security_alarm = 0;

        // AI 逻辑
        if(current_pm.is_new) { 
             float ai_input_data[4]; 
             ai_input_data[0] = (float)temperature;
             ai_input_data[1] = (float)humidity;
             ai_input_data[2] = (float)current_pm.particles_0_3um;
             ai_input_data[3] = (float)current_pm.particles_2_5um;
             
             int8_t anomaly_result = ai_model_predict(ai_input_data); 
             if (anomaly_result == -1) g_ai_alarm = 1;
             else g_ai_alarm = 0;
        }

        Alarm_Check_Env(temperature, humidity, current_pm.pm2_5_std);
        
        // C. 界面与执行
        UI_Display_Main(temperature, humidity, current_pm.pm2_5_std, distance_mm, light_val);
        Alarm_Update(); // 更新灯光和蜂鸣器
        
        delay_ms(100); 
    }
}

//-------------------- 辅助函数实现 --------------------//

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
        AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L);
        AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L);
        AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_NUM); 
    }
}

void UI_Display_Main(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light)
{
    char text_buffer[40];

    // 数据显示
    POINT_COLOR = BLACK;
    sprintf(text_buffer, "Temp: %d C   ", temp);
    LCD_ShowString(30, 40, 200, 16, 16, (u8*)text_buffer);
    sprintf(text_buffer, "Humi: %d %%   ", humi);
    LCD_ShowString(30, 60, 200, 16, 16, (u8*)text_buffer);
    sprintf(text_buffer, "PM2.5: %d ug/m3  ", pm2_5);
    LCD_ShowString(30, 80, 210, 16, 16, (u8*)text_buffer);
    
    sprintf(text_buffer, "Dist: %d mm   ", dist);
    LCD_ShowString(30, 100, 200, 16, 16, (u8*)text_buffer);
    sprintf(text_buffer, "Light: %d %%   ", light);
    LCD_ShowString(30, 120, 200, 16, 16, (u8*)text_buffer);

    // AI状态
    if(g_ai_alarm) {
        POINT_COLOR = RED;
        LCD_ShowString(30, 150, 210, 16, 16, (u8*)"[AI ALERT: FIRE!]   ");
    } else {
        POINT_COLOR = GREEN;
        LCD_ShowString(30, 150, 210, 16, 16, (u8*)"[AI System: Normal] ");
    }

    // 显示灯光模式
    POINT_COLOR = BLUE;
    if(g_light_mode == MODE_AUTO)      LCD_ShowString(160, 150, 100, 16, 16, (u8*)"Mode: AUTO ");
    else if(g_light_mode == MODE_EMERGENCY) LCD_ShowString(160, 150, 100, 16, 16, (u8*)"Mode: LIGHT");
    else                               LCD_ShowString(160, 150, 100, 16, 16, (u8*)"Mode: OFF  ");

    // 阈值设置显示
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

// 按键处理逻辑 (已更新)
void Key_Process(void)
{
    u8 key = KEY_Scan(0);
    
    if (key == KEY0_PRES) { // KEY0: 菜单控制
        if (!g_is_setting_mode) {
            // 如果不在设置模式，进入设置模式
            g_is_setting_mode = 1;
            g_current_param = PARAM_TEMP_H;
        } else {
            // 如果在设置模式，切换下一个参数
            g_current_param++;
            // 如果超过最后一个参数，退出设置模式
            if (g_current_param >= PARAM_EXIT) {
                g_is_setting_mode = 0;
            }
        }
    } 
    else if (key == KEY1_PRES) { // KEY1: 增加 / 切换灯光模式
        if (g_is_setting_mode) {
            // 设置模式下：数值增加
            switch(g_current_param) {
                case PARAM_TEMP_H: if(temp_H<99) temp_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H); break;
                case PARAM_TEMP_L: if(temp_L<temp_H) temp_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L); break;
                case PARAM_HUMI_H: if(humi_H<100) humi_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H); break;
                case PARAM_HUMI_L: if(humi_L<humi_H) humi_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L); break;
                case PARAM_PM25_H: if(pm25_H<255) pm25_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H); break;
            }
        } else {
            // 【新增】监控模式下：切换灯光模式 (Auto -> Emergency -> Off)
            g_light_mode++;
            if (g_light_mode > MODE_OFF) g_light_mode = MODE_AUTO;
        }
    } 
    else if (key == WKUP_PRES) { // WK_UP: 减少
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

void Alarm_Check_Env(u8 temp, u8 humi, u16 pm2_5)
{
    g_temp_alarm = (temp > temp_H || temp < temp_L);
    g_humi_alarm = (humi > humi_H || humi < humi_L);
    g_pm_alarm   = (pm2_5 > pm25_H);
}

// 报警逻辑 (已更新灯光模式)
void Alarm_Update(void)
{
    static u16 tick = 0;
    tick++;
    BEEP = 0; // 强制静音

    // 设置模式：青色流水灯
    if (g_is_setting_mode) { 
        if(tick % 2 == 0) { 
            WS2812_Set_All_Color(0,0,0);
            WS2812_Set_Pixel_Color( (tick/2) % LED_NUM, 0, 50, 50);
            WS2812_Refresh();
        }
        return;
    }

    // --- 优先处理应急模式 ---
    if (g_light_mode == MODE_EMERGENCY) {
        // 应急模式：全白高亮常亮
        WS2812_Set_All_Color(255, 255, 255); 
        WS2812_Refresh();
        return; 
    }

    // --- 报警逻辑 (即使是OFF模式，报警也会闪烁，确保安全) ---
    
    // 1. AI 火灾 (红色爆闪)
    if (g_ai_alarm) { 
        if (tick % 1 == 0) { 
            WS2812_Set_All_Color(255, 0, 0); 
            WS2812_Refresh();
        } else { WS2812_Clear(); }
        return;
    } 
    // 2. 安防入侵 (紫色闪烁)
    else if (g_security_alarm) {
        if (tick % 5 == 0) { 
            WS2812_Set_All_Color(128, 0, 128); 
            WS2812_Refresh();
        } else if (tick % 5 == 1) { WS2812_Clear(); }
        return;
    }
    // 3. PM超标 (橙色)
    else if (g_pm_alarm) { 
        if (tick % 2 == 0) {
            WS2812_Set_All_Color(200, 60, 0); 
            WS2812_Refresh();
        } else { WS2812_Clear(); }
        return;
    } 
    // 4. 温度 (黄色)
    else if (g_temp_alarm) {
        if (tick % 5 == 0) {
            WS2812_Set_All_Color(200, 100, 0); 
            WS2812_Refresh();
        } else if (tick % 5 == 1) { WS2812_Clear(); }
        return;
    } 
    // 5. 湿度 (蓝色)
    else if (g_humi_alarm) {
        if (tick % 10 == 0) {
            WS2812_Set_All_Color(0, 0, 200); 
            WS2812_Refresh();
        } else if (tick % 10 == 1) { WS2812_Clear(); }
        return;
    } 

    // --- 正常待机状态 ---
    if (g_light_mode == MODE_AUTO) {
        // 自动模式：绿色常亮
        WS2812_Set_All_Color(0, 50, 0); 
        WS2812_Refresh();
    } else {
        // 关闭模式：熄灭
        WS2812_Clear();
    }
}
