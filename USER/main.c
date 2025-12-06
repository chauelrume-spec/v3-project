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
#include "sdio_sdcard.h"
#include "malloc.h"     
#include "ff.h"         
#include "exfuns.h"     
#include "piclib.h"     
#include <stdio.h>

//==================================================================
//             UI 坐标与颜色配置 (适配 480x800)
//==================================================================

// --- 1. 颜色配置 (关键修改) ---
// RGB565转换宏: 将电脑上的RGB888转换为LCD用的RGB565
#define RGB565(r, g, b) ((u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)))

// 【请修改这里】: 使用吸管工具吸取您背景图数据显示区域的颜色
// 这里预设了一个深蓝灰色，您可以根据实际图片修改 R, G, B 的值
#define DATA_BG_COLOR   RGB565(10, 15, 30) 

// 文字颜色 (背景深色时建议用白色，背景浅色时用黑色)
#define DATA_TEXT_COLOR WHITE 

// --- 2. 顶部标题 ---
#define UI_TITLE_X      85   
#define UI_TITLE_Y      25
#define UI_AUTHOR_X     150  
#define UI_AUTHOR_Y     55

// --- 3. 实时数据区 (左侧栏) ---
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

// --- 4. 状态指示区 (右侧栏) ---
#define UI_STATUS_X     260
#define UI_STATUS_Y     110  
#define UI_MODE_X       260
#define UI_MODE_Y       150  

// --- 5. 阈值设置区 (双列布局) ---
#define SETTING_TITLE_X 30
#define SETTING_TITLE_Y 340

#define SET_COL1_X      30   
#define SET_COL2_X      240  
#define SET_START_Y     370
#define SET_ROW_H       30

// --- 6. 底部按键说明 ---
#define HELP_START_X    40
#define HELP_START_Y    680
#define HELP_ROW_H      40

//==================================================================

// --- EEPROM地址 ---
#define EEPROM_ADDR_TEMP_H  0x01
#define EEPROM_ADDR_TEMP_L  0x02
#define EEPROM_ADDR_HUMI_H  0x03
#define EEPROM_ADDR_HUMI_L  0x04
#define EEPROM_ADDR_PM25_H  0x05
#define EEPROM_ADDR_MAGIC   0x00
#define EEPROM_MAGIC_NUM    0xAA

// --- 系统状态 ---
typedef enum {
    PARAM_TEMP_H,
    PARAM_TEMP_L,
    PARAM_HUMI_H,
    PARAM_HUMI_L,
    PARAM_PM25_H
} SettingParam_t;

// --- 灯光模式 ---
typedef enum {
    MODE_AUTO = 0,
    MODE_EMERGENCY,
    MODE_OFF
} LightMode_t;

static u8 g_is_setting_mode = 0;
static SettingParam_t g_current_param = PARAM_TEMP_H;
static LightMode_t g_light_mode = MODE_AUTO;

// --- 阈值 ---
static u16 temp_H = 30;
static u16 temp_L = 10;
static u16 humi_H = 80;
static u16 humi_L = 40;
static u16 pm25_H = 75;

// --- 报警标志 ---
static u8 g_temp_alarm = 0;
static u8 g_humi_alarm = 0;
static u8 g_pm_alarm = 0;
static u8 g_ai_alarm = 0;
static u8 g_security_alarm = 0;

// --- 函数声明 ---
void System_Init_All(void);
void Load_Thresholds(void);
void UI_Draw_Background(void);
void UI_Update_Data(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light);
void Key_Process(void);
void Alarm_Update(void);

//------------------------------------------------------------------
//                            主 函 数
//------------------------------------------------------------------
int main(void)
{
    u8 temperature = 0, humidity = 0;
    PMS_Data_t current_pm;
    u32 distance_mm = 0;
    u8 light_val = 0;

    // 1. 初始化
    System_Init_All();

    // 2. 加载阈值
    Load_Thresholds();

    // 3. 加载背景图片
    UI_Draw_Background();

    while(1)
    {
        // --- A. 数据采集 ---
        DHT11_Read_Data(&temperature, &humidity);
        current_pm = PMS7003_Get_Data();
        
        static u8 dist_tick = 0;
        if(++dist_tick > 2) {
            distance_mm = HCSR04_Get_Distance();
            dist_tick = 0;
        }
        light_val = Lsens_Get_Val();

        // --- B. 按键处理 ---
        Key_Process();

        // --- C. 逻辑判断 ---
        // 安防
        if (distance_mm < 500 || light_val > 20) g_security_alarm = 1;
        else g_security_alarm = 0;

        // AI & 常规报警
        if(current_pm.is_new) {
             float ai_data[4] = {(float)temperature, (float)humidity,
                                 (float)current_pm.particles_0_3um, (float)current_pm.particles_2_5um};
             if (ai_model_predict(ai_data) == -1) g_ai_alarm = 1;
             else g_ai_alarm = 0;

             // 刷新UI (仅在有新PM数据时刷新)
             UI_Update_Data(temperature, humidity, current_pm.pm2_5_std, distance_mm, light_val);
        }

        // 阈值检查
        g_temp_alarm = (temperature > temp_H || temperature < temp_L);
        g_humi_alarm = (humidity > humi_H || humidity < humi_L);
        g_pm_alarm   = (current_pm.pm2_5_std > pm25_H);

        // --- D. 执行报警 ---
        Alarm_Update();

        delay_ms(100);
    }
}

//------------------------------------------------------------------
//                          功能函数实现
//------------------------------------------------------------------

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
    // 加载图片 BG.JPG
    u8 res = ai_load_picfile("0:/BG.JPG", 0, 0, lcddev.width, lcddev.height, 1);
    
    // 如果加载失败，用白色清屏兜底
    if(res) {
        LCD_Clear(WHITE);
    }
    
    // 如果图片加载失败，绘制简易文字框
    // 如果成功，这部分其实会被图片覆盖或作为补充
    BACK_COLOR = WHITE; 
    POINT_COLOR = BLACK;
    if(res) { 
        LCD_ShowString(UI_TITLE_X, UI_TITLE_Y, 400, 24, 24, (u8*)"WAREHOUSE SECURITY SYSTEM");
        LCD_ShowString(UI_AUTHOR_X, UI_AUTHOR_Y, 400, 16, 16, (u8*)"Intelligent AI Monitor");
    }
    
    // 底部按键说明 (通常画在图片上更好，这里作为备用或叠加)
    // 我们可以把它设为透明叠加，但F103慢，还是带底色画比较稳
    POINT_COLOR = BLUE;
    BACK_COLOR  = WHITE; // 这里的底色如果能跟图片底部一致最好
    LCD_ShowString(HELP_START_X, HELP_START_Y, 400, 24, 24, (u8*)"KEY0 : MENU / NEXT");
    LCD_ShowString(HELP_START_X, HELP_START_Y + HELP_ROW_H, 400, 24, 24, (u8*)"KEY1 : [+] / LIGHT");
    LCD_ShowString(HELP_START_X, HELP_START_Y + HELP_ROW_H*2, 400, 24, 24, (u8*)"WK_UP: [-] / DEC");
}

void UI_Update_Data(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light)
{
    POINT_COLOR = DATA_TEXT_COLOR; // 白色 (由宏定义决定)
    BACK_COLOR  = DATA_BG_COLOR;   // 深色 (由宏定义决定，模拟透明)

    char buf[50];

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

    // 2. AI 状态
    if(g_ai_alarm) {
        POINT_COLOR = RED;
        LCD_ShowString(UI_STATUS_X, UI_STATUS_Y, 200, 24, 24, (u8*)"[ AI: FIRE! ] ");
    } else {
        POINT_COLOR = GREEN;
        LCD_ShowString(UI_STATUS_X, UI_STATUS_Y, 200, 24, 24, (u8*)"[ AI: OK ]    ");
    }

    // 3. 灯光模式
    POINT_COLOR = BLUE;
    if(g_light_mode == MODE_AUTO)      LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Light: AUTO ]");
    else if(g_light_mode == MODE_EMERGENCY) LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Light: ON ]  ");
    else                               LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Light: OFF ] ");

    // 4. 阈值设置 (这里通常不需要伪透明，保持白底黑字更清晰，或者也用深底白字)
    // 这里为了清晰，我先保持白底黑字，您可以根据需要把下面的 BACK_COLOR 改为 DATA_BG_COLOR
    BACK_COLOR = WHITE; 
    POINT_COLOR = BLACK;
    
    LCD_ShowString(SETTING_TITLE_X, SETTING_TITLE_Y, 200, 16, 16, (u8*)"-- SYSTEM SETTINGS --");
    
    u16 y = SET_START_Y;
    
    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_TEMP_H) ? RED : BLACK;
    sprintf(buf, "Temp H: %d", temp_H);
    LCD_ShowString(SET_COL1_X, y, 200, 16, 16, (u8*)buf);

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_TEMP_L) ? RED : BLACK;
    sprintf(buf, "Temp L: %d", temp_L);
    LCD_ShowString(SET_COL2_X, y, 200, 16, 16, (u8*)buf);
    y += SET_ROW_H;

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_HUMI_H) ? RED : BLACK;
    sprintf(buf, "Humi H: %d", humi_H);
    LCD_ShowString(SET_COL1_X, y, 200, 16, 16, (u8*)buf);

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_HUMI_L) ? RED : BLACK;
    sprintf(buf, "Humi L: %d", humi_L);
    LCD_ShowString(SET_COL2_X, y, 200, 16, 16, (u8*)buf);
    y += SET_ROW_H;

    POINT_COLOR = (g_is_setting_mode && g_current_param == PARAM_PM25_H) ? RED : BLACK;
    sprintf(buf, "PM2.5 Max: %d", pm25_H);
    LCD_ShowString(SET_COL1_X, y, 200, 16, 16, (u8*)buf);

    // 恢复默认颜色
    POINT_COLOR = BLACK;
    BACK_COLOR = WHITE;
}

void Key_Process(void)
{
    u8 key = KEY_Scan(0);
    
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
    else if (key == WKUP_PRES) { 
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

void Alarm_Update(void)
{
    static u16 tick = 0;
    tick++;
    BEEP = 0; // 静音

    if (g_is_setting_mode) { 
        if(tick % 2 == 0) {
            WS2812_Set_All_Color(0,0,0);
            WS2812_Set_Pixel_Color( (tick/2) % LED_NUM, 0, 50, 50); // 青色流水
            WS2812_Refresh();
        }
        return;
    }

    if (g_light_mode == MODE_EMERGENCY) {
        WS2812_Set_All_Color(255, 255, 255); 
        WS2812_Refresh();
        return; 
    }

    if (g_ai_alarm) { 
        if (tick % 1 == 0) { WS2812_Set_All_Color(255, 0, 0); WS2812_Refresh(); }
        else WS2812_Clear();
    } 
    else if (g_security_alarm) { 
        if (tick % 5 == 0) { WS2812_Set_All_Color(128, 0, 128); WS2812_Refresh(); }
        else if (tick % 5 == 1) WS2812_Clear();
    }
    else if (g_pm_alarm) { 
        if (tick % 2 == 0) { WS2812_Set_All_Color(200, 60, 0); WS2812_Refresh(); }
        else WS2812_Clear();
    }
    else if (g_temp_alarm) { 
        if (tick % 5 == 0) { WS2812_Set_All_Color(200, 100, 0); WS2812_Refresh(); }
        else if (tick % 5 == 1) WS2812_Clear();
    }
    else if (g_humi_alarm) { 
        if (tick % 10 == 0) { WS2812_Set_All_Color(0, 0, 200); WS2812_Refresh(); }
        else if (tick % 10 == 1) WS2812_Clear();
    } 
    else { 
        if (g_light_mode == MODE_AUTO) {
            WS2812_Set_All_Color(0, 50, 0); 
            WS2812_Refresh();
        } else {
            WS2812_Clear();
        }
    }
}
