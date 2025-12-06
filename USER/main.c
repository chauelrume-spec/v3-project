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
#include "rtc.h"        // 【新增】RTC实时时钟
#include "sdio_sdcard.h"
#include "malloc.h"     
#include "ff.h"         
#include "exfuns.h"     
#include "piclib.h"     
#include <stdio.h>

//==================================================================
//             UI 坐标配置区域
//==================================================================

#define RGB565(r, g, b) ((u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)))
#define DATA_BG_COLOR   RGB565(10, 15, 30) 
#define DATA_TEXT_COLOR WHITE 

// --- 顶部区域 ---
#define UI_TITLE_X      20   
#define UI_TITLE_Y      25
#define UI_TIME_X       320  // 【新增】时间显示坐标
#define UI_TIME_Y       25

// --- 数据区 ---
#define DATA_LEFT_X     30   
#define DATA_START_Y    110
#define DATA_ROW_H      40   

#define UI_TEMP_X       DATA_LEFT_X
#define UI_TEMP_Y       (DATA_START_Y + 0 * DATA_ROW_H) 
#define UI_HUMI_X       DATA_LEFT_X
#define UI_HUMI_Y       (DATA_START_Y + 1 * DATA_ROW_H) 
#define UI_PM25_X       DATA_LEFT_X
#define UI_PM25_Y       (DATA_START_Y + 2 * DATA_ROW_H) 
#define UI_DIST_X       DATA_LEFT_X
#define UI_DIST_Y       (DATA_START_Y + 3 * DATA_ROW_H) 
#define UI_LIGHT_X      DATA_LEFT_X
#define UI_LIGHT_Y      (DATA_START_Y + 4 * DATA_ROW_H) 

// --- 状态区 ---
#define UI_STATUS_X     260
#define UI_STATUS_Y     110  
#define UI_MODE_X       260
#define UI_MODE_Y       150
#define UI_BEEP_X       260  
#define UI_BEEP_Y       190  // 静音状态显示

// --- 设置区 ---
#define SETTING_TITLE_X 30
#define SETTING_TITLE_Y 340
#define SET_COL1_X      30   
#define SET_COL2_X      240  
#define SET_START_Y     370
#define SET_ROW_H       30

// --- 底部 ---
#define HELP_START_X    40
#define HELP_START_Y    680
#define HELP_ROW_H      40

//==================================================================

#define EEPROM_ADDR_TEMP_H  0x01
#define EEPROM_ADDR_TEMP_L  0x02
#define EEPROM_ADDR_HUMI_H  0x03
#define EEPROM_ADDR_HUMI_L  0x04
#define EEPROM_ADDR_PM25_H  0x05
#define EEPROM_ADDR_MAGIC   0x00
#define EEPROM_MAGIC_NUM    0xAA

typedef enum {
    PARAM_TEMP_H,
    PARAM_TEMP_L,
    PARAM_HUMI_H,
    PARAM_HUMI_L,
    PARAM_PM25_H
} SettingParam_t;

typedef enum {
    MODE_AUTO = 0,
    MODE_EMERGENCY,
    MODE_OFF
} LightMode_t;

static u8 g_is_setting_mode = 0;
static SettingParam_t g_current_param = PARAM_TEMP_H;
static LightMode_t g_light_mode = MODE_AUTO;

// 【关键修改】静音模式标志位 (0:有声, 1:静音)
static u8 g_silent_mode = 0; 

static u16 temp_H = 30;
static u16 temp_L = 10;
static u16 humi_H = 80;
static u16 humi_L = 40;
static u16 pm25_H = 75;

static u8 g_temp_alarm = 0;
static u8 g_humi_alarm = 0;
static u8 g_pm_alarm = 0;
static u8 g_ai_alarm = 0;
static u8 g_security_alarm = 0;

void System_Init_All(void);
void Load_Thresholds(void);
void UI_Draw_Background(void);
void UI_Update_Data(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light);
void Key_Process(void);
void Alarm_Update(void);

int main(void)
{
    u8 temperature = 0, humidity = 0;
    PMS_Data_t current_pm;
    u32 distance_mm = 0;
    u8 light_val = 0;

    System_Init_All();
    Load_Thresholds();
    UI_Draw_Background();
    
    UI_Update_Data(temperature, humidity, 0, 0, 0);

    while(1)
    {
        // A. 数据采集
        DHT11_Read_Data(&temperature, &humidity);
        current_pm = PMS7003_Get_Data();
        
        static u8 dist_tick = 0;
        if(++dist_tick > 3) { 
            distance_mm = HCSR04_Get_Distance();
            dist_tick = 0;
        }
        light_val = Lsens_Get_Val();

        // B. 按键处理
        Key_Process();

        // C. 逻辑判断
        if (distance_mm < 500 || light_val > 90) g_security_alarm = 1;
        else g_security_alarm = 0;

        static u8 ui_tick = 0;
        ui_tick++;
        if (ui_tick >= 5 || current_pm.is_new) 
        {
             ui_tick = 0;
             float ai_data[4] = {(float)temperature, (float)humidity,
                                 (float)current_pm.particles_0_3um, (float)current_pm.particles_2_5um};
             if (ai_model_predict(ai_data) == -1) g_ai_alarm = 1;
             else g_ai_alarm = 0;

             // 刷新UI (包含时间显示)
             UI_Update_Data(temperature, humidity, current_pm.pm2_5_std, distance_mm, light_val);
        }

        g_temp_alarm = (temperature > temp_H || temperature < temp_L);
        g_humi_alarm = (humidity > humi_H || humidity < humi_L);
        g_pm_alarm   = (current_pm.pm2_5_std > pm25_H);

        // D. 报警执行
        Alarm_Update();

        delay_ms(100);
    }
}

void System_Init_All(void)
{
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
    
    // 初始化RTC
    RTC_Init(); 

    my_mem_init(SRAMIN);
    while(SD_Init()) {
        LCD_ShowString(30, 30, 200, 16, 16, (u8*)"SD Card Error!");
        delay_ms(500);
    }
    exfuns_init();
    f_mount(fs[0], "0:", 1);
    piclib_init();
}

void Load_Thresholds(void)
{
    if (AT24CXX_ReadOneByte(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC_NUM) {
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

void UI_Draw_Background(void)
{
    u8 res = ai_load_picfile("0:/BG.JPG", 0, 0, lcddev.width, lcddev.height, 1);
    if(res) LCD_Clear(WHITE);
    
    if(res) { 
        POINT_COLOR = BLACK;
        LCD_ShowString(UI_TITLE_X, 25, 400, 24, 24, (u8*)"WAREHOUSE SECURITY");
    }
    
    POINT_COLOR = BLUE;
    BACK_COLOR  = (res == 0) ? DATA_BG_COLOR : WHITE; 
    if(res) BACK_COLOR = WHITE; 

    LCD_ShowString(HELP_START_X, HELP_START_Y, 400, 24, 24, (u8*)"KEY0 : MENU / NEXT");
    LCD_ShowString(HELP_START_X, HELP_START_Y + HELP_ROW_H, 400, 24, 24, (u8*)"KEY1 : [+] / LIGHT");
    LCD_ShowString(HELP_START_X, HELP_START_Y + HELP_ROW_H*2, 400, 24, 24, (u8*)"WK_UP: [-] / MUTE"); // 更新说明
}

void UI_Update_Data(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light)
{
    POINT_COLOR = DATA_TEXT_COLOR;
    BACK_COLOR  = DATA_BG_COLOR; 

    char buf[50];
    
    // 【新增】显示时间 (右上角)
    // 假设 rtc.c 中定义了 calendar 结构体
    sprintf(buf, "%02d:%02d:%02d", calendar.hour, calendar.min, calendar.sec);
    LCD_ShowString(UI_TIME_X, UI_TIME_Y, 200, 24, 24, (u8*)buf);

    // 1. 实时数据
    sprintf(buf, "Temp : %d C   ", temp);
    LCD_ShowString(UI_TEMP_X, UI_TEMP_Y, 400, 24, 24, (u8*)buf);
    sprintf(buf, "Humi : %d %%   ", humi);
    LCD_ShowString(UI_HUMI_X, UI_HUMI_Y, 400, 24, 24, (u8*)buf);
    sprintf(buf, "PM2.5: %d ug/m3  ", pm2_5);
    LCD_ShowString(UI_PM25_X, UI_PM25_Y, 400, 24, 24, (u8*)buf);
    sprintf(buf, "Dist : %d mm   ", dist);
    LCD_ShowString(UI_DIST_X, UI_DIST_Y, 400, 24, 24, (u8*)buf);
    sprintf(buf, "Light: %d %%   ", light);
    LCD_ShowString(UI_LIGHT_X, UI_LIGHT_Y, 400, 24, 24, (u8*)buf);

    // 2. 状态区
    if(g_ai_alarm) {
        POINT_COLOR = RED; LCD_ShowString(UI_STATUS_X, UI_STATUS_Y, 200, 24, 24, (u8*)"[ AI: FIRE! ] ");
    } else {
        POINT_COLOR = GREEN; LCD_ShowString(UI_STATUS_X, UI_STATUS_Y, 200, 24, 24, (u8*)"[ AI: OK ]    ");
    }

    POINT_COLOR = BLUE;
    if(g_light_mode == MODE_AUTO)      LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Lit: AUTO ]");
    else if(g_light_mode == MODE_EMERGENCY) LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Lit: ON ]  ");
    else                               LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Lit: OFF ] ");

    // 【新增】显示声音状态 (MUTE / SOUND)
    if(g_silent_mode) {
        POINT_COLOR = RED; LCD_ShowString(UI_BEEP_X, UI_BEEP_Y, 200, 24, 24, (u8*)"[ Mode: MUTE ] ");
    } else {
        POINT_COLOR = GREEN; LCD_ShowString(UI_BEEP_X, UI_BEEP_Y, 200, 24, 24, (u8*)"[ Mode: SOUND ]");
    }

    // 3. 阈值设置
    BACK_COLOR = WHITE; 
    POINT_COLOR = BLACK;
    LCD_ShowString(SETTING_TITLE_X, SETTING_TITLE_Y, 200, 16, 16, (u8*)"-- SYSTEM SETTINGS --");
    
    u16 y = SET_START_Y;
    
    #define SHOW_PARAM(param_enum, name, val, col_x) \
        POINT_COLOR = (g_is_setting_mode && g_current_param == param_enum) ? RED : BLACK; \
        sprintf(buf, "%s: %d%s", name, val, (g_is_setting_mode && g_current_param == param_enum) ? " <<" : "   "); \
        LCD_ShowString(col_x, y, 220, 16, 16, (u8*)buf);

    SHOW_PARAM(PARAM_TEMP_H, "Temp H", temp_H, SET_COL1_X);
    SHOW_PARAM(PARAM_TEMP_L, "Temp L", temp_L, SET_COL2_X);
    y += SET_ROW_H;
    SHOW_PARAM(PARAM_HUMI_H, "Humi H", humi_H, SET_COL1_X);
    SHOW_PARAM(PARAM_HUMI_L, "Humi L", humi_L, SET_COL2_X);
    y += SET_ROW_H;
    SHOW_PARAM(PARAM_PM25_H, "PM2.5 Max", pm25_H, SET_COL1_X);

    POINT_COLOR = BLACK;
    BACK_COLOR = WHITE;
}

void Key_Process(void)
{
    u8 key = KEY_Scan(0);
    
    // KEY0: 菜单 / 切换
    if (key == KEY0_PRES) { 
        if (!g_is_setting_mode) {
            g_is_setting_mode = 1; 
            g_current_param = PARAM_TEMP_H;
        } else {
            g_current_param++;
            if (g_current_param > PARAM_PM25_H) {
                g_is_setting_mode = 0; 
            }
        }
    } 
    // KEY1: 增加 / 切换灯光
    else if (key == KEY1_PRES) { 
        if (g_is_setting_mode) {
            switch(g_current_param) {
                case PARAM_TEMP_H: if(temp_H<99) temp_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H); break;
                case PARAM_TEMP_L: if(temp_L<temp_H) temp_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L); break;
                case PARAM_HUMI_H: if(humi_H<100) humi_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H); break;
                case PARAM_HUMI_L: if(humi_L<humi_H) humi_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L); break;
                case PARAM_PM25_H: if(pm25_H<999) pm25_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H); break;
            }
        } else {
            g_light_mode++;
            if (g_light_mode > MODE_OFF) g_light_mode = MODE_AUTO;
        }
    } 
    // WK_UP (KEY2): 减少 / 静音开关
    else if (key == WKUP_PRES) { 
        if (g_is_setting_mode) {
             switch(g_current_param) {
                case PARAM_TEMP_H: if(temp_H>temp_L) temp_H--; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H); break;
                case PARAM_TEMP_L: if(temp_L>0) temp_L--; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L); break;
                case PARAM_HUMI_H: if(humi_H>humi_L) humi_H--; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H); break;
                case PARAM_HUMI_L: if(humi_L>0) humi_L--; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L); break;
                case PARAM_PM25_H: if(pm25_H>0) pm25_H--; AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H); break;
            }
        } else {
            // 【新增】监控模式：切换静音模式 (MUTE / SOUND)
            g_silent_mode = !g_silent_mode;
        }
    }
}

// 报警逻辑 (包含5秒自动关闭声音 + 手动静音)
void Alarm_Update(void)
{
    static u16 tick = 0;
    static u16 beep_duration_cnt = 0; // 蜂鸣器持续时间计数器
    u8 is_any_alarm = 0; // 是否有任何报警
    u8 enable_sound = 0; // 最终是否允许发声

    tick++;

    // 1. 判断是否有报警
    if (g_ai_alarm || g_security_alarm || g_pm_alarm || g_temp_alarm || g_humi_alarm) {
        is_any_alarm = 1;
    }

    // 2. 计时器逻辑 (关键)
    if (is_any_alarm) {
        if (beep_duration_cnt < 65000) { // 防止溢出
            beep_duration_cnt++;
        }
    } else {
        beep_duration_cnt = 0; // 没有报警时，重置计时器
    }

    // 3. 声音开关逻辑
    // 允许发声的条件：(未开启静音模式) AND (报警持续时间 < 50个周期/5秒)
    // 假设 delay_ms(100) -> 1秒=10次 -> 5秒=50次
    if (!g_silent_mode && beep_duration_cnt < 50) {
        enable_sound = 1;
    } else {
        enable_sound = 0; // 静音模式 或 超过5秒
    }
    
    // 强制先关蜂鸣器 (后面根据 enable_sound 决定是否开启)
    BEEP = 0; 

    // 设置模式：青色流水
    if (g_is_setting_mode) { 
        if(tick % 2 == 0) {
            WS2812_Set_All_Color(0,0,0);
            WS2812_Set_Pixel_Color( (tick/2) % LED_NUM, 0, 50, 50); 
            WS2812_Refresh();
        }
        return;
    }
    
    // 应急照明
    if (g_light_mode == MODE_EMERGENCY) {
        WS2812_Set_All_Color(255, 255, 255); 
        WS2812_Refresh();
        return; 
    }

    // 报警动作
    // 1. AI 火灾 (红闪)
    if (g_ai_alarm) { 
        if (tick % 1 == 0) { 
            if(enable_sound) BEEP = !BEEP; // 只有在允许发声时才响
            WS2812_Set_All_Color(255, 0, 0); 
            WS2812_Refresh(); 
        } else { WS2812_Clear(); }
    } 
    // 2. 入侵 (紫闪)
    else if (g_security_alarm) { 
        if (tick % 5 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(128, 0, 128); 
            WS2812_Refresh(); 
        } else if (tick % 5 == 1) WS2812_Clear();
    }
    // 3. PM (橙闪)
    else if (g_pm_alarm) { 
        if (tick % 2 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(200, 60, 0); 
            WS2812_Refresh(); 
        } else WS2812_Clear();
    }
    // 4. 温度 (黄闪)
    else if (g_temp_alarm) { 
        if (tick % 5 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(200, 100, 0); 
            WS2812_Refresh(); 
        } else if (tick % 5 == 1) WS2812_Clear();
    }
    // 5. 湿度 (蓝闪)
    else if (g_humi_alarm) { 
        if (tick % 10 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(0, 0, 200); 
            WS2812_Refresh(); 
        } else if (tick % 10 == 1) WS2812_Clear();
    } 
    else { 
        // 正常
        if (g_light_mode == MODE_AUTO) {
            WS2812_Set_All_Color(0, 50, 0); 
            WS2812_Refresh();
        } else {
            WS2812_Clear(); 
        }
    }
}