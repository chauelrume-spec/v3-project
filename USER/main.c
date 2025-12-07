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
#include "rtc.h"        
#include "sdio_sdcard.h"
#include "malloc.h"     
#include "ff.h"         
#include "exfuns.h"     
#include "piclib.h"     
#include <stdio.h>

//==================================================================
//             UI 界面配置区域 (适配 480x800 竖屏)
//==================================================================

// --- 颜色定义 ---
// RGB565转换宏: 将电脑RGB颜色转为LCD颜色
#define RGB565(r, g, b) ((u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)))
#define DATA_BG_COLOR   RGB565(10, 15, 30)  // 数据背景色(深蓝灰)，用于擦除旧数字
#define DATA_TEXT_COLOR WHITE               // 数据文字颜色

// --- 顶部标题区坐标 ---
#define UI_TITLE_X      85   
#define UI_TITLE_Y      25
#define UI_TIME_X       350  // 时间显示位置(右上角)
#define UI_TIME_Y       30

// --- 左侧：实时数据区坐标 ---
#define DATA_LEFT_X     30   
#define DATA_START_Y    110
#define DATA_ROW_H      40   // 行高

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

// --- 右侧：状态显示区坐标 ---
// 状态图标 (必须是 100x100 像素的 JPG 图片)
#define UI_ICON_X       280  
#define UI_ICON_Y       100  
#define UI_ICON_W       100  
#define UI_ICON_H       100

// 状态文字
#define UI_STATUS_TEXT_X 260
#define UI_STATUS_TEXT_Y 210

// 其他状态信息
#define UI_MODE_X       260
#define UI_MODE_Y       240
#define UI_BEEP_X       260  
#define UI_BEEP_Y       270  

// --- 中部：阈值设置区坐标 ---
#define SETTING_TITLE_X 30
#define SETTING_TITLE_Y 340
#define SET_COL1_X      30   
#define SET_COL2_X      240  
#define SET_START_Y     370
#define SET_ROW_H       30

// --- 底部：操作说明区坐标 ---
#define HELP_START_X    40
#define HELP_START_Y    680
#define HELP_ROW_H      40

//==================================================================

// --- EEPROM 存储地址定义 (AT24C02) ---
#define EEPROM_ADDR_TEMP_H  0x01
#define EEPROM_ADDR_TEMP_L  0x02
#define EEPROM_ADDR_HUMI_H  0x03
#define EEPROM_ADDR_HUMI_L  0x04
#define EEPROM_ADDR_PM25_H  0x05
#define EEPROM_ADDR_MAGIC   0x00 // 首次运行标志位
#define EEPROM_MAGIC_NUM    0xAA 

// --- 枚举：设置项 ---
typedef enum {
    PARAM_TEMP_H,
    PARAM_TEMP_L,
    PARAM_HUMI_H,
    PARAM_HUMI_L,
    PARAM_PM25_H
} SettingParam_t;

// --- 枚举：灯光模式 ---
typedef enum {
    MODE_AUTO = 0,      // 自动(报警闪烁)
    MODE_EMERGENCY,     // 应急照明(全白)
    MODE_OFF            // 关闭(仅报警闪烁)
} LightMode_t;

// --- 枚举：系统整体状态 (用于切换图标) ---
typedef enum {
    STATUS_NORMAL = 0,  // 正常 (IC_OK.JPG)
    STATUS_FIRE,        // AI火灾 (IC_FIRE.JPG)
    STATUS_INTRUSION,   // 入侵 (IC_SEC.JPG)
    STATUS_WARNING      // 环境超标 (IC_WARN.JPG)
} SystemStatus_t;

// --- 全局变量定义 ---
static u8 g_is_setting_mode = 0;   // 是否处于设置模式
static SettingParam_t g_current_param = PARAM_TEMP_H; // 当前设置的参数
static LightMode_t g_light_mode = MODE_AUTO; // 灯光模式
static u8 g_silent_mode = 0;       // 静音模式 (1=静音)
static SystemStatus_t g_sys_status = STATUS_NORMAL; // 当前系统状态

// 阈值变量 (默认值)
static u16 temp_H = 30;  
static u16 temp_L = 10;  
static u16 humi_H = 80;  
static u16 humi_L = 40;  
static u16 pm25_H = 75;  

// 报警标志位 (1=触发)
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
void UI_Update_Status_Icon(void); // 图标刷新
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

    // 1. 硬件初始化
    System_Init_All();

    // 2. 从EEPROM加载阈值
    Load_Thresholds();

    // 3. 绘制背景图片
    UI_Draw_Background();
    
    // 4. 首次刷新界面数据 (避免白屏)
    UI_Update_Data(0, 0, 0, 0, 0);
    UI_Update_Status_Icon(); 

    while(1)
    {
        // ===========================
        // A. 数据采集
        // ===========================
        DHT11_Read_Data(&temperature, &humidity);
        current_pm = PMS7003_Get_Data();
        
        // 降低采样频率 (超声波和光敏不需要太快)
        static u8 dist_tick = 0;
        if(++dist_tick > 3) { 
            distance_mm = HCSR04_Get_Distance();
            dist_tick = 0;
        }
        light_val = Lsens_Get_Val();
        
        // 刷新RTC时间
        RTC_Get(); 

        // ===========================
        // B. 交互与逻辑
        // ===========================
        
        // 1. 物理按键处理
        Key_Process();

        // 2. 安防逻辑判断
        // 距离<50cm 或 光强>20(夜间亮光) -> 触发入侵报警
        if (distance_mm < 500 || light_val > 90) g_security_alarm = 1;
        else g_security_alarm = 0;

        // 3. AI 推理 & UI刷新 (每500ms执行一次)
        static u8 ui_tick = 0;
        ui_tick++;
        if (ui_tick >= 5 || current_pm.is_new) 
        {
             ui_tick = 0;
             // 准备AI输入: [温度, 湿度, 0.3um颗粒, 2.5um颗粒]
             float ai_data[4] = {(float)temperature, (float)humidity,
                                 (float)current_pm.particles_0_3um, (float)current_pm.particles_2_5um};
             // 执行AI预测 (-1:异常, 1:正常)
             if (ai_model_predict(ai_data) == -1) g_ai_alarm = 1;
             else g_ai_alarm = 0;

             // 刷新屏幕文字数据
             UI_Update_Data(temperature, humidity, current_pm.pm2_5_std, distance_mm, light_val);
        }

        // 4. 常规阈值检查
        g_temp_alarm = (temperature > temp_H || temperature < temp_L);
        g_humi_alarm = (humidity > humi_H || humidity < humi_L);
        g_pm_alarm   = (current_pm.pm2_5_std > pm25_H);
        
        // 5. 状态仲裁 (确定当前应该显示什么图标)
        if (g_ai_alarm)             g_sys_status = STATUS_FIRE;      // 优先级1: 火灾
        else if (g_security_alarm)  g_sys_status = STATUS_INTRUSION; // 优先级2: 入侵
        else if (g_pm_alarm || g_temp_alarm || g_humi_alarm) 
                                    g_sys_status = STATUS_WARNING;   // 优先级3: 警告
        else                        g_sys_status = STATUS_NORMAL;    // 优先级4: 正常

        // 6. 刷新状态图标 (只在状态改变时刷新)
        UI_Update_Status_Icon();

        // ===========================
        // C. 执行报警
        // ===========================
        Alarm_Update();

        delay_ms(100);
    }
}

//------------------------------------------------------------------
//                          功能函数实现
//------------------------------------------------------------------

// 硬件初始化总成
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
    RTC_Init(); 

    // SD卡与文件系统
    my_mem_init(SRAMIN);
    while(SD_Init()) {
        LCD_ShowString(30, 30, 200, 16, 16, (u8*)"SD Card Error!");
        delay_ms(500);
    }
    exfuns_init();
    f_mount(fs[0], "0:", 1);
    piclib_init();
}

// 从EEPROM加载参数
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

void UI_Draw_Chinese_Text(void)
{
    // ============================================================
    // 1. 顶部项目名称 (32号字体, 蓝色, 居中或左上)
    // ============================================================
    POINT_COLOR = BLUE; // 设置为蓝色
    
    // 坐标策略：(X, Y)
    // 假设从 (60, 30) 开始显示，每个字宽32，间隔0
    u16 title_y = 30;
    u16 title_x = 80; // 稍微居中一点 ((480 - 8*32)/2 = 112, 这里设80偏左一点)

    // 调用 LCD_ShowChinese(x, y, num, size)
    // num 对应 Chinese_32x32 数组中的第几个字 (从1开始)
    LCD_ShowChinese(title_x + 32*0, title_y, 1, 32); // 仓
    LCD_ShowChinese(title_x + 32*1, title_y, 2, 32); // 库
    LCD_ShowChinese(title_x + 32*2, title_y, 3, 32); // 安
    LCD_ShowChinese(title_x + 32*3, title_y, 4, 32); // 全
    LCD_ShowChinese(title_x + 32*4, title_y, 5, 32); // 预
    LCD_ShowChinese(title_x + 32*5, title_y, 6, 32); // 警
    LCD_ShowChinese(title_x + 32*6, title_y, 7, 32); // 系
    LCD_ShowChinese(title_x + 32*7, title_y, 8, 32); // 统


    // ============================================================
    // 2. 底部个人信息 (16号字体, 黑色, 右下角)
    // ============================================================
    POINT_COLOR = BLACK; // 设置为黑色
    
    // 坐标计算：
    // 屏幕宽480, 高800
    // 右下角留出边距: Right_Margin = 20, Bottom_Margin = 20
    // 文字大概宽度: 8个字 * 16 = 128像素 (估算)
    // 起始 X ≈ 480 - 160 = 320
    // 起始 Y ≈ 800 - 60 = 740
    
    u16 info_x = 300; // 右侧起始位置
    u16 info_y = 700; // 底部起始位置
    u16 row_h  = 20;  // 行高

    // --- 第一行：姓名 ---
    // 对应 Chinese_16x16 数组
    LCD_ShowChinese(info_x + 16*0, info_y, 9, 16); // 姓 (Chinese_16x16里第1个)
    LCD_ShowChinese(info_x + 16*1, info_y, 10, 16); // 名 (Chinese_16x16里第2个)
    LCD_ShowString(info_x + 16*2, info_y, 16, 16, 16, (u8*)":");
    
    // 您的名字 (假设2个字, 对应数组第3,4个)
    LCD_ShowChinese(info_x + 16*2 + 8, info_y, 13, 16); // [名1]
    LCD_ShowChinese(info_x + 16*3 + 8, info_y, 14, 16); // [名2]
    LCD_ShowChinese(info_x + 16*4 + 8, info_y, 15, 16); // [名2]
    // --- 第二行：学号 ---
    u16 id_y = info_y + row_h;
    LCD_ShowChinese(info_x + 16*0, id_y, 11, 16); // 学 (Chinese_16x16里第5个)
    LCD_ShowChinese(info_x + 16*1, id_y, 12, 16); // 号 (Chinese_16x16里第6个)
    // 学号数字是ASCII，直接用ShowString
    LCD_ShowString(info_x + 16*2, id_y, 200, 16, 16, (u8*)":23001040215"); 
}

// 绘制背景
void UI_Draw_Background(void)
{
    u8 res = ai_load_picfile("0:/BG.JPG", 0, 0, lcddev.width, lcddev.height, 1);
    if(res) LCD_Clear(WHITE);
	  
		// 绘制中文标题和信息
    UI_Draw_Chinese_Text();
    
    if(res) { // 如果没图，显示简易标题
        POINT_COLOR = BLACK;
        LCD_ShowString(UI_TITLE_X, 25, 400, 24, 24, (u8*)"WAREHOUSE SECURITY");
    }
    
    // 绘制按键说明
    POINT_COLOR = BLUE;
    BACK_COLOR  = (res == 0) ? DATA_BG_COLOR : WHITE; 
    if(res) BACK_COLOR = WHITE; 

    LCD_ShowString(HELP_START_X, HELP_START_Y, 400, 24, 24, (u8*)"KEY0 : MENU / NEXT");
    LCD_ShowString(HELP_START_X, HELP_START_Y + HELP_ROW_H, 400, 24, 24, (u8*)"KEY1 : [+] / LIGHT");
    LCD_ShowString(HELP_START_X, HELP_START_Y + HELP_ROW_H*2, 400, 24, 24, (u8*)"KEY2 : [-] / MUTE / LCD"); 
}



// 【核心】状态图标刷新逻辑
void UI_Update_Status_Icon(void)
{
    static SystemStatus_t last_status = (SystemStatus_t)255; // 缓存上一次状态
    
    if (g_sys_status != last_status)
    {
        BACK_COLOR = DATA_BG_COLOR; 
        
        switch(g_sys_status)
        {
            case STATUS_NORMAL:
                ai_load_picfile("0:/IC_OK.JPG", UI_ICON_X, UI_ICON_Y, UI_ICON_W, UI_ICON_H, 1);
                POINT_COLOR = GREEN;
                LCD_ShowString(UI_STATUS_TEXT_X, UI_STATUS_TEXT_Y, 200, 24, 24, (u8*)"SYSTEM SAFE    ");
                break;
            case STATUS_FIRE:
                ai_load_picfile("0:/IC_FIRE.JPG", UI_ICON_X, UI_ICON_Y, UI_ICON_W, UI_ICON_H, 1);
                POINT_COLOR = RED;
                LCD_ShowString(UI_STATUS_TEXT_X, UI_STATUS_TEXT_Y, 200, 24, 24, (u8*)"FIRE ALERT!    ");
                break;
            case STATUS_INTRUSION:
                ai_load_picfile("0:/IC_SEC.JPG", UI_ICON_X, UI_ICON_Y, UI_ICON_W, UI_ICON_H, 1);
                POINT_COLOR = 0xF81F; // 紫色
                LCD_ShowString(UI_STATUS_TEXT_X, UI_STATUS_TEXT_Y, 200, 24, 24, (u8*)"INTRUDER ALERT ");
                break;
            case STATUS_WARNING:
                ai_load_picfile("0:/IC_WARN.JPG", UI_ICON_X, UI_ICON_Y, UI_ICON_W, UI_ICON_H, 1);
                POINT_COLOR = 0xFD20; // 橙色
                LCD_ShowString(UI_STATUS_TEXT_X, UI_STATUS_TEXT_Y, 200, 24, 24, (u8*)"ENV WARNING    ");
                break;
        }
        last_status = g_sys_status;
    }
}

// 刷新数据文字
void UI_Update_Data(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light)
{
    POINT_COLOR = DATA_TEXT_COLOR;
    BACK_COLOR  = DATA_BG_COLOR; 

    char buf[50];
    
    // 1. 时间 (静音时变红提醒)
    POINT_COLOR = (g_silent_mode) ? WHITE : DATA_TEXT_COLOR;
    sprintf(buf, "%02d:%02d:%02d", calendar.hour, calendar.min, calendar.sec);
    LCD_ShowString(UI_TIME_X, UI_TIME_Y, 200, 24, 24, (u8*)buf);
    POINT_COLOR = DATA_TEXT_COLOR;

    // 2. 实时数据
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

    // 3. 状态信息文字
    POINT_COLOR = BLUE;
    if(g_light_mode == MODE_AUTO)      LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Lit: AUTO ]");
    else if(g_light_mode == MODE_EMERGENCY) LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Lit: ON ]  ");
    else                               LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Lit: OFF ] ");

    if(g_silent_mode) {
        POINT_COLOR = RED; LCD_ShowString(UI_BEEP_X, UI_BEEP_Y, 200, 24, 24, (u8*)"[ Mode: MUTE ] ");
    } else {
        POINT_COLOR = GREEN; LCD_ShowString(UI_BEEP_X, UI_BEEP_Y, 200, 24, 24, (u8*)"[ Mode: SOUND ]");
    }

    // 4. 阈值设置 (选中项高亮)
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

// 按键处理
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
    // WK_UP (KEY2): 减少 / 静音开关 / 屏幕开关
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
            g_silent_mode = !g_silent_mode; // 切换静音
            // LCD_LED = !LCD_LED; // 如果需要同时开关屏幕，请取消此行注释
        }
    }
    // KEY2: 控制屏幕
    else if (key == KEY2_PRES) { LCD_LED = !LCD_LED; }
}

// 报警逻辑
void Alarm_Update(void)
{
    static u16 tick = 0;
    static u16 beep_duration_cnt = 0; 
    u8 is_any_alarm = 0; 
    u8 enable_sound = 0; 

    tick++;

    if (g_ai_alarm || g_security_alarm || g_pm_alarm || g_temp_alarm || g_humi_alarm) {
        is_any_alarm = 1;
    }

    if (is_any_alarm) {
        if (beep_duration_cnt < 65000) beep_duration_cnt++;
    } else {
        beep_duration_cnt = 0; 
    }

    // 声音控制：未静音 且 报警时长 < 5秒
    if (!g_silent_mode && beep_duration_cnt < 50) {
        enable_sound = 1;
    } else {
        enable_sound = 0; 
    }
    
    BEEP = 0; 

    if (g_is_setting_mode) { 
        if(tick % 2 == 0) {
            WS2812_Set_All_Color(0,0,0);
            WS2812_Set_Pixel_Color( (tick/2) % LED_NUM, 0, 50, 50); 
            WS2812_Refresh();
        }
        return;
    }

    if (g_light_mode == MODE_EMERGENCY) {
        WS2812_Set_All_Color(255, 255, 255); 
        WS2812_Refresh();
        return; 
    }

    // 1. AI 火灾
    if (g_ai_alarm) { 
        if (tick % 1 == 0) { 
            if(enable_sound) BEEP = !BEEP; 
            WS2812_Set_All_Color(255, 0, 0); 
            WS2812_Refresh(); 
        } else { WS2812_Clear(); }
    } 
    // 2. 入侵
    else if (g_security_alarm) { 
        if (tick % 5 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(128, 0, 128); 
            WS2812_Refresh(); 
        } else if (tick % 5 == 1) WS2812_Clear();
    }
    // 3. PM
    else if (g_pm_alarm) { 
        if (tick % 2 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(200, 60, 0); 
            WS2812_Refresh(); 
        } else WS2812_Clear();
    }
    // 4. 温度
    else if (g_temp_alarm) { 
        if (tick % 5 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(200, 100, 0); 
            WS2812_Refresh(); 
        } else if (tick % 5 == 1) WS2812_Clear();
    }
    // 5. 湿度
    else if (g_humi_alarm) { 
        if (tick % 10 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(0, 0, 200); 
            WS2812_Refresh(); 
        } else if (tick % 10 == 1) WS2812_Clear();
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
