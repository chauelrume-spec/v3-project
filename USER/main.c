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
#include "stm32f10x_iwdg.h" // 【核心】看门狗支持
#include <stdio.h>
#include <string.h>
//==================================================================
//             UI 坐标与颜色配置 (适配 480x800 竖屏 - 最终版)
//==================================================================

// --- 颜色定义 ---
#define RGB565(r, g, b) ((u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)))
#define DATA_BG_COLOR_DEF  WHITE        // 默认背景色
#define DATA_TEXT_COLOR_DEF BLACK       // 默认文字色
#define DATA_BG_COLOR_IMG   RGB565(10, 15, 30) // 图片背景色
#define DATA_TEXT_COLOR_IMG WHITE       // 图片背景文字色

static u16 g_text_color = DATA_TEXT_COLOR_DEF; // 当前文字颜色
static u16 g_bg_color   = DATA_BG_COLOR_DEF;   // 当前背景颜色

// --- 顶部标题区坐标 ---
#define UI_TITLE_X      85   // 中文标题起始X
#define UI_TITLE_Y      25   // 中文标题起始Y
#define UI_TIME_X       350  // 时间显示位置(右上角)
#define UI_TIME_Y       30

// --- 左侧：实时数据区坐标 ---
#define DATA_LEFT_X     30   
#define DATA_START_Y    110
#define DATA_ROW_H      40   // 数据行高

#define UI_TEMP_X       DATA_LEFT_X
#define UI_TEMP_Y       (DATA_START_Y + 0 * DATA_ROW_H) // 温度显示Y
#define UI_HUMI_X       DATA_LEFT_X
#define UI_HUMI_Y       (DATA_START_Y + 1 * DATA_ROW_H) // 湿度显示Y
#define UI_PM25_X       DATA_LEFT_X
#define UI_PM25_Y       (DATA_START_Y + 2 * DATA_ROW_H) // PM2.5显示Y
#define UI_DIST_X       DATA_LEFT_X
#define UI_DIST_Y       (DATA_START_Y + 3 * DATA_ROW_H) // 距离显示Y
#define UI_LIGHT_X      DATA_LEFT_X
#define UI_LIGHT_Y      (DATA_START_Y + 4 * DATA_ROW_H) // 光照显示Y

// --- 右侧：状态显示区坐标 ---
#define UI_ICON_X       190  // 状态图标X
#define UI_ICON_Y       500  // 状态图标Y
#define UI_ICON_W       100  // 图标宽度
#define UI_ICON_H       100  // 图标高度

#define UI_STATUS_TEXT_X 140 // 图标下方文字X
#define UI_ICON_TEXT_Y  610  // 图标下方文字Y

#define UI_MODE_X       260  // 灯光模式显示X
#define UI_MODE_Y       240  // 灯光模式显示Y
#define UI_BEEP_X       260  // 声音模式显示X
#define UI_BEEP_Y       270  // 声音模式显示Y
#define UI_ERR_X        260  // 硬件错误代码显示X
#define UI_ERR_Y        300  // 硬件错误代码显示Y

// --- 中部：阈值设置区坐标 ---
#define SETTING_TITLE_X 30   // 设置标题X
#define SETTING_TITLE_Y 340  // 设置标题Y
#define SET_COL1_X      30   // 设置列1 X
#define SET_COL2_X      240  // 设置列2 X
#define SET_START_Y     370  // 设置起始Y
#define SET_ROW_H       30   // 设置行高

// --- 底部：操作说明区坐标 ---
#define HELP_START_X    40   // 帮助文字起始X
#define HELP_START_Y    680  // 帮助文字起始Y
#define HELP_ROW_H      40   // 帮助行高

// --- 个人信息区域 ---
#define INFO_NAME_X     300  // 姓名显示X
#define INFO_NAME_Y     740  // 姓名显示Y
#define INFO_ID_X       300  // 学号显示X
#define INFO_ID_Y       760  // 学号显示Y

//==================================================================

// EEPROM存储地址定义
#define EEPROM_ADDR_TEMP_H  0x01    // 温度上限存储地址
#define EEPROM_ADDR_TEMP_L  0x02    // 温度下限存储地址
#define EEPROM_ADDR_HUMI_H  0x03    // 湿度上限存储地址
#define EEPROM_ADDR_HUMI_L  0x04    // 湿度下限存储地址
#define EEPROM_ADDR_PM25_H  0x05    // PM2.5上限存储地址
#define EEPROM_ADDR_MAGIC   0x00    // 魔术字地址(用于判断是否首次使用)
#define EEPROM_MAGIC_NUM    0xAA    // 魔术字数值

// 设置参数枚举
typedef enum {
    PARAM_TEMP_H,   // 温度上限
    PARAM_TEMP_L,   // 温度下限
    PARAM_HUMI_H,   // 湿度上限
    PARAM_HUMI_L,   // 湿度下限
    PARAM_PM25_H    // PM2.5上限
} SettingParam_t;

// 灯光模式枚举
typedef enum {
    MODE_AUTO = 0,      // 自动模式
    MODE_EMERGENCY,     // 应急模式(常亮)
    MODE_OFF            // 关闭模式
} LightMode_t;

// 系统状态枚举
typedef enum {
    STATUS_NORMAL = 0,  // 正常状态
    STATUS_FIRE,        // 火灾报警
    STATUS_INTRUSION,   // 入侵报警
    STATUS_WARNING      // 环境异常警告
} SystemStatus_t;

// 全局状态变量
static u8 g_is_setting_mode = 0;             // 设置模式标志(0:正常 1:设置)
static SettingParam_t g_current_param = PARAM_TEMP_H; // 当前设置参数
static LightMode_t g_light_mode = MODE_AUTO; // 灯光模式
static u8 g_silent_mode = 0;                 // 静音模式(0:关闭 1:开启)
static SystemStatus_t g_sys_status = STATUS_NORMAL; // 系统状态

// --- 硬件状态标志位 (0:正常, 1:异常) ---
static u8 g_err_sd = 0;        // SD卡故障标志
static u8 g_err_dht11 = 0;     // DHT11故障标志
static u8 g_err_pms = 0;       // PMS7003故障标志

// 阈值默认值
static u16 temp_H = 30;    // 温度上限默认值
static u16 temp_L = 10;    // 温度下限默认值
static u16 humi_H = 80;    // 湿度上限默认值
static u16 humi_L = 40;    // 湿度下限默认值
static u16 pm25_H = 75;    // PM2.5上限默认值

// 报警标志位
static u8 g_temp_alarm = 0;    // 温度报警
static u8 g_humi_alarm = 0;    // 湿度报警
static u8 g_pm_alarm = 0;      // PM2.5报警
static u8 g_ai_alarm = 0;      // AI火灾报警
static u8 g_security_alarm = 0;// 安防入侵报警

// 声明串口接收相关的外部变量 (在 usart.h 中定义)
extern u8  USART_RX_BUF[200]; // 假设 USART_REC_LEN 是 200
extern u16 USART_RX_STA;       // 串口接收状态

// --- 函数声明 ---
void System_Init_All(void);        // 系统全部初始化
void Load_Thresholds(void);        // 加载EEPROM阈值
void UI_Draw_Background(void);     // 绘制UI背景
void UI_Draw_Chinese_Text(void);   // 中文绘制函数
void UI_Update_Data(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light); // 更新数据显示
void UI_Update_Status_Icon(void);  // 更新状态图标
void Key_Process(void);            // 按键处理
void Alarm_Update(void);           // 报警逻辑处理
void IWDG_Init(u8 prer,u16 rlr);   // 看门狗初始化
void USART_Process_Command(u8 *Rx_Buf, u16 Rx_Status);
void Serial_Data_Report(u8 temp, u8 humi, u16 pm2_5);
//------------------------------------------------------------------
//                            主 函 数
//------------------------------------------------------------------
int main(void)
{
    u8 temperature = 0, humidity = 0;    // 温湿度缓存
    PMS_Data_t current_pm;               // PM2.5数据缓存
    u32 distance_mm = 0;                 // 距离数据(mm)
    u8 light_val = 0;                    // 光照值
    u8 pms_timeout_cnt = 0;              // PMS7003超时计数器

    // 1. 系统初始化
    System_Init_All();

    // 2. 从EEPROM加载阈值配置
    Load_Thresholds();

    // 3. 绘制背景 (自适应SD卡状态)
    UI_Draw_Background();
    
    // 4. 首次UI刷新
    UI_Update_Data(0, 0, 0, 0, 0);
    UI_Update_Status_Icon(); 

    // 开启看门狗(分频4,重载值625,溢出时间: 4*625*128us=320ms)
    IWDG_Init(4, 625); 

    while(1)
    {
        // A. 传感器数据采集
        // DHT11温湿度采集
        if(g_err_dht11 == 0) {
             if(DHT11_Read_Data(&temperature, &humidity)) { }
        }
        // PM2.5数据采集
        current_pm = PMS7003_Get_Data();
        
        // PMS7003心跳检测(超时3秒判定故障)
        if (current_pm.is_new) {
            pms_timeout_cnt = 0;     // 收到新数据,重置计数器
            g_err_pms = 0;           // 清除故障标志
        } else {
            pms_timeout_cnt++;
            if(pms_timeout_cnt > 30) { // 30*100ms=3秒超时
                g_err_pms = 1;         // 标记PMS故障
                pms_timeout_cnt = 30;  // 防止溢出
                g_ai_alarm = 0;        // 故障时关闭AI报警
            }
        }
        
        // 距离传感器采集(400ms一次,降低刷新率)
        static u8 dist_tick = 0;
        if(++dist_tick > 3) { 
            distance_mm = HCSR04_Get_Distance();
            dist_tick = 0;
        }
        // 光照传感器采集
        light_val = Lsens_Get_Val();
        
        // RTC时间更新
        RTC_Get(); 

        // B. 按键交互处理
        Key_Process();

        // 安防报警判断(距离<500mm 或 光照>90%判定入侵)
        if (distance_mm < 500 || light_val > 90) g_security_alarm = 1;
        else g_security_alarm = 0;
				static u8 report_tick = 0; // 串口上报计数器
        
        // UI刷新控制(500ms一次 或 PMS有新数据时立即刷新)
        static u8 ui_tick = 0;
        ui_tick++;
				report_tick++; // 每100ms递增一次
				
				// 检查串口是否接收到完整指令 (0x8000 标志完整接收)
        if(USART_RX_STA & 0x8000) 
        {
            USART_Process_Command(USART_RX_BUF, USART_RX_STA);
        }
				
        if (ui_tick >= 5 || current_pm.is_new|| g_err_pms || g_err_dht11) 
        {
             ui_tick = 0;
             
             // AI火灾推理(仅PMS正常时执行)
             if (g_err_pms == 0) {
                 float ai_data[4] = {(float)temperature, (float)humidity,
                                     (float)current_pm.particles_0_3um, (float)current_pm.particles_2_5um};
                 // AI返回-1表示火灾风险
                 if (ai_model_predict(ai_data) == -1) g_ai_alarm = 1;
                 else g_ai_alarm = 0;
             } else {
                 g_ai_alarm = 0; // PMS故障时关闭AI报警
             }

             // 刷新UI数据显示
             UI_Update_Data(temperature, humidity, current_pm.pm2_5_std, distance_mm, light_val);
        }
				
				// 串口数据上报 (每 1000ms / 10个周期)
        if (report_tick >= 10)
        {
            report_tick = 0;
            Serial_Data_Report(temperature, humidity, current_pm.pm2_5_std);
        }
				
        // 阈值超限判断(忽略故障传感器)
        if(g_err_dht11 == 0) {
            g_temp_alarm = (temperature > temp_H || temperature < temp_L); // 温度超限
            g_humi_alarm = (humidity > humi_H || humidity < humi_L);       // 湿度超限
        } else { 
            g_temp_alarm = 0; 
            g_humi_alarm = 0; 
        }
        
        if(g_err_pms == 0) {
            g_pm_alarm = (current_pm.pm2_5_std > pm25_H); // PM2.5超限
        } else { 
            g_pm_alarm = 0; 
        }
        
        // 系统状态仲裁(优先级:硬件故障>AI火灾>入侵>环境超限>正常)
        if (g_err_pms || g_err_dht11) 
            g_sys_status = STATUS_WARNING;      // 硬件故障-警告
        else if (g_ai_alarm)                  
            g_sys_status = STATUS_FIRE;         // AI火灾-火警
        else if (g_security_alarm)             
            g_sys_status = STATUS_INTRUSION;    // 入侵-安防报警
        else if (g_pm_alarm || g_temp_alarm || g_humi_alarm) 
            g_sys_status = STATUS_WARNING;      // 环境超限-警告
        else                                  
            g_sys_status = STATUS_NORMAL;       // 正常状态

        // 更新状态图标显示
        UI_Update_Status_Icon();

        // C. 报警执行(蜂鸣器+WS2812灯带)
        Alarm_Update();

        // 喂看门狗
        IWDG_ReloadCounter();
        
        // 主循环延时(100ms)
        delay_ms(100);
    }
}

//------------------------------------------------------------------
//                          功能函数实现
//------------------------------------------------------------------

/**
 * @brief  独立看门狗初始化
 * @param  prer: 预分频系数(0~7),实际分频=4^prer
 * @param  rlr: 重载值(0~0xFFF)
 * @retval 无
 */
void IWDG_Init(u8 prer,u16 rlr) 
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable); // 使能写访问
    IWDG_SetPrescaler(prer);                     // 设置预分频
    IWDG_SetReload(rlr);                         // 设置重载值
    IWDG_ReloadCounter();                        // 重载计数器
    IWDG_Enable();                               // 开启看门狗
}

/**
 * @brief  系统全部初始化
 * @note   初始化所有外设,设置故障标志
 * @retval 无
 */
void System_Init_All(void)
{
    delay_init();                      // 延时初始化
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); // 中断分组
    uart_init(115200);                 // 串口初始化
    LED_Init();                        // LED初始化
    LCD_Init();                        // LCD初始化
    KEY_Init();                        // 按键初始化
    BEEP_Init();                       // 蜂鸣器初始化
    AT24CXX_Init();                    // EEPROM初始化
    PMS7003_Init();                    // PM2.5传感器初始化
    WS2812_Init();                     // WS2812灯带初始化
    ai_model_init();                   // AI模型初始化
    HCSR04_Init();                     // 超声波传感器初始化
    Lsens_Init();                      // 光敏传感器初始化
    RTC_Init();                        // RTC时钟初始化

    // SD卡与文件系统初始化 (非阻塞)
    my_mem_init(SRAMIN);               // 内存初始化
    if(SD_Init()) {
        g_err_sd = 1;                  // SD卡初始化失败
    } else {
        g_err_sd = 0;
        exfuns_init();                 // 文件系统扩展初始化
        f_mount(fs[0], "0:", 1);       // 挂载SD卡
        piclib_init();                 // 图片库初始化
    }
    
    // DHT11初始化 (非阻塞)
    if(DHT11_Init()) {
        g_err_dht11 = 1;               // DHT11初始化失败
    } else {
        g_err_dht11 = 0;
    }
}

/**
 * @brief  从EEPROM加载阈值配置
 * @note   首次使用写入默认值,非首次读取存储值
 * @retval 无
 */
void Load_Thresholds(void)
{
    // 检查魔术字(判断是否首次使用)
    if (AT24CXX_ReadOneByte(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC_NUM) {
        // 读取存储的阈值
        temp_H = AT24CXX_ReadOneByte(EEPROM_ADDR_TEMP_H);
        temp_L = AT24CXX_ReadOneByte(EEPROM_ADDR_TEMP_L);
        humi_H = AT24CXX_ReadOneByte(EEPROM_ADDR_HUMI_H);
        humi_L = AT24CXX_ReadOneByte(EEPROM_ADDR_HUMI_L);
        pm25_H = AT24CXX_ReadOneByte(EEPROM_ADDR_PM25_H);
    } else {
        // 首次使用,写入默认值
        AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L);
        AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L);
        AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H);
        AT24CXX_WriteOneByte(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_NUM);
    }
}

/**
 * @brief  绘制中文文字
 * @note   绘制标题和个人信息
 * @retval 无
 */
void UI_Draw_Chinese_Text(void)
{
    // === 1. 顶部项目名称 "仓库安全预警系统" (32号字体, 蓝色) ===
    POINT_COLOR = BLUE; 
    u16 title_y = UI_TITLE_Y;
    u16 title_x = UI_TITLE_X;

    LCD_ShowChinese(title_x + 32*0, title_y, 1, 32); // 仓
    LCD_ShowChinese(title_x + 32*1, title_y, 2, 32); // 库
    LCD_ShowChinese(title_x + 32*2, title_y, 3, 32); // 安
    LCD_ShowChinese(title_x + 32*3, title_y, 4, 32); // 全
    LCD_ShowChinese(title_x + 32*4, title_y, 5, 32); // 预
    LCD_ShowChinese(title_x + 32*5, title_y, 6, 32); // 警
    LCD_ShowChinese(title_x + 32*6, title_y, 7, 32); // 系
    LCD_ShowChinese(title_x + 32*7, title_y, 8, 32); // 统

    // === 2. 底部右下角：个人信息 "姓名 学号" (16号字体, 黑色) ===
    POINT_COLOR = BLACK;
    
    u16 info_x = INFO_NAME_X; 
    u16 info_y = INFO_NAME_Y; 
    //u16 row_h  = 20;

    // 姓名
    LCD_ShowChinese(info_x + 16*0, info_y, 9, 16);   // 姓
    LCD_ShowChinese(info_x + 16*1, info_y, 10, 16);  // 名
    LCD_ShowString(info_x + 16*2, info_y, 16, 16, 16, (u8*)":"); 
    LCD_ShowChinese(info_x + 16*2 + 8, info_y, 13, 16); // 具
    LCD_ShowChinese(info_x + 16*3 + 8, info_y, 14, 16); // 体
	LCD_ShowChinese(info_x + 16*4 + 8, info_y, 15, 16); // 姓
                                                         // 名

    // 学号
    u16 id_y = INFO_ID_Y;
    LCD_ShowChinese(info_x + 16*0, id_y, 11, 16);  // 学
    LCD_ShowChinese(info_x + 16*1, id_y, 12, 16);  // 号
    LCD_ShowString(info_x + 16*2, id_y, 200, 16, 16, (u8*)":23001040215"); 
}

/**
 * @brief  绘制UI背景
 * @note   SD卡正常则加载背景图,否则绘制简易界面
 * @retval 无
 */
void UI_Draw_Background(void)
{
    u8 res = 1; // 图片加载结果(1:失败 0:成功)
    
    // 只有SD卡正常才加载图片
    if(g_err_sd == 0) {
        res = ai_load_picfile("0:/BG.JPG", 0, 0, lcddev.width, lcddev.height, 1);
    }
    
    // 自适应UI：SD卡故障或加载失败时，绘制简易界面
    if(g_err_sd || res) { 
        LCD_Clear(WHITE);
        g_bg_color = DATA_BG_COLOR_DEF;
        g_text_color = DATA_TEXT_COLOR_DEF;
        
        // 绘制备用 UI 框
        POINT_COLOR = BLUE;
        LCD_DrawRectangle(10, 90, 240, 320);  // 数据区框
        LCD_DrawRectangle(250, 90, 470, 320); // 状态区框
        
        UI_Draw_Chinese_Text(); // 绘制中文

        // SD卡故障提示
        if(g_err_sd) {
             POINT_COLOR = RED;
             LCD_ShowString(30, 60, 400, 16, 16, (u8*)"[WARN] SD CARD FAILED!");
        }
    } else {
        // 加载成功，使用深色模式
        g_bg_color = DATA_BG_COLOR_IMG;
        g_text_color = DATA_TEXT_COLOR_IMG;
        UI_Draw_Chinese_Text(); // 绘制中文
    }
    
    // 底部按键说明
    POINT_COLOR = BLUE;
    BACK_COLOR  = (g_err_sd || res) ? WHITE : g_bg_color; 

    LCD_ShowString(HELP_START_X, HELP_START_Y, 400, 24, 24, (u8*)"KEY0 : MENU / NEXT");
    LCD_ShowString(HELP_START_X, HELP_START_Y + HELP_ROW_H, 400, 24, 24, (u8*)"KEY1 : [+] / LIGHT");
    LCD_ShowString(HELP_START_X, HELP_START_Y + HELP_ROW_H*2, 400, 24, 24, (u8*)"WK_UP: [-] / MUTE"); 
}

/**
 * @brief  更新状态图标显示
 * @note   根据系统状态加载对应图标和文字
 * @retval 无
 */
void UI_Update_Status_Icon(void)
{
    static SystemStatus_t last_status = (SystemStatus_t)255; // 上一次状态
    
    if(g_err_sd) return; // SD卡故障时不更新图标

    // 状态变化时才更新
    if (g_sys_status != last_status)
    {
        BACK_COLOR = g_bg_color; 
        
        switch(g_sys_status)
        {
            case STATUS_NORMAL:
                ai_load_picfile("0:/IC_OK.JPG", UI_ICON_X, UI_ICON_Y, UI_ICON_W, UI_ICON_H, 1);
                POINT_COLOR = GREEN;
                LCD_ShowString(UI_STATUS_TEXT_X, UI_ICON_TEXT_Y, 200, 24, 24, (u8*)"SYSTEM SAFE    ");
                break;
            case STATUS_FIRE:
                ai_load_picfile("0:/IC_FIRE.JPG", UI_ICON_X, UI_ICON_Y, UI_ICON_W, UI_ICON_H, 1);
                POINT_COLOR = RED;
                LCD_ShowString(UI_STATUS_TEXT_X, UI_ICON_TEXT_Y, 200, 24, 24, (u8*)"FIRE ALERT!    ");
                break;
            case STATUS_INTRUSION:
                ai_load_picfile("0:/IC_SEC.JPG", UI_ICON_X, UI_ICON_Y, UI_ICON_W, UI_ICON_H, 1);
                POINT_COLOR = 0xF81F; // 品红色
                LCD_ShowString(UI_STATUS_TEXT_X, UI_ICON_TEXT_Y, 200, 24, 24, (u8*)"INTRUDER ALERT ");
                break;
            case STATUS_WARNING:
                ai_load_picfile("0:/IC_WARN.JPG", UI_ICON_X, UI_ICON_Y, UI_ICON_W, UI_ICON_H, 1);
                POINT_COLOR = 0xFD20; // 橙色
                LCD_ShowString(UI_STATUS_TEXT_X, UI_ICON_TEXT_Y, 200, 24, 24, (u8*)"ENV WARNING    ");
                break;
        }
        last_status = g_sys_status;
    }
}

/**
 * @brief  更新数据显示
 * @param  temp: 温度值
 * @param  humi: 湿度值
 * @param  pm2_5: PM2.5值
 * @param  dist: 距离值(mm)
 * @param  light: 光照值(%)
 * @retval 无
 */
void UI_Update_Data(u8 temp, u8 humi, u16 pm2_5, u32 dist, u8 light)
{
    POINT_COLOR = g_text_color;
    BACK_COLOR  = g_bg_color; 

    char buf[50];
    
    // 时间显示(静音模式显示白色)
    POINT_COLOR = (g_silent_mode) ? WHITE : g_text_color;
    sprintf(buf, "%02d:%02d:%02d", calendar.hour, calendar.min, calendar.sec);
    LCD_ShowString(UI_TIME_X, UI_TIME_Y, 200, 24, 24, (u8*)buf);
    
    POINT_COLOR = g_text_color;

    // 1. 实时数据 (带故障显示)
    if(g_err_dht11) {
        LCD_ShowString(UI_TEMP_X, UI_TEMP_Y, 400, 24, 24, (u8*)"Temp : Err       ");
        LCD_ShowString(UI_HUMI_X, UI_HUMI_Y, 400, 24, 24, (u8*)"Humi : Err       ");
    } else {
        sprintf(buf, "Temp : %d C   ", temp);
        LCD_ShowString(UI_TEMP_X, UI_TEMP_Y, 400, 24, 24, (u8*)buf);
        sprintf(buf, "Humi : %d %%   ", humi);
        LCD_ShowString(UI_HUMI_X, UI_HUMI_Y, 400, 24, 24, (u8*)buf);
    }
    
    if(g_err_pms) {
        LCD_ShowString(UI_PM25_X, UI_PM25_Y, 400, 24, 24, (u8*)"PM2.5: Err       ");
    } else {
        sprintf(buf, "PM2.5: %d ug/m3  ", pm2_5);
        LCD_ShowString(UI_PM25_X, UI_PM25_Y, 400, 24, 24, (u8*)buf);
    }
    
    // 距离和光照显示
    sprintf(buf, "Dist : %d mm   ", dist);
    LCD_ShowString(UI_DIST_X, UI_DIST_Y, 400, 24, 24, (u8*)buf);
    sprintf(buf, "Light: %d %%   ", light);
    LCD_ShowString(UI_LIGHT_X, UI_LIGHT_Y, 400, 24, 24, (u8*)buf);

    // 2. 硬件故障代码显示
    POINT_COLOR = RED;
    BACK_COLOR = g_bg_color; 
    if(g_err_sd) 
        LCD_ShowString(UI_ERR_X, UI_ERR_Y, 200, 16, 16, (u8*)"E:SD");
    else if(g_err_dht11) 
        LCD_ShowString(UI_ERR_X, UI_ERR_Y, 200, 16, 16, (u8*)"E:DHT");
    else if(g_err_pms) 
        LCD_ShowString(UI_ERR_X, UI_ERR_Y, 200, 16, 16, (u8*)"E:PMS");
    else 
        LCD_ShowString(UI_ERR_X, UI_ERR_Y, 200, 16, 16, (u8*)"     "); 

    // 3. 灯光/声音模式显示
    POINT_COLOR = BLUE;
    if(g_light_mode == MODE_AUTO)      
        LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Lit: AUTO ]");
    else if(g_light_mode == MODE_EMERGENCY) 
        LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Lit: ON ]  ");
    else                               
        LCD_ShowString(UI_MODE_X, UI_MODE_Y, 200, 24, 24, (u8*)"[ Lit: OFF ] ");

    // 静音模式显示
    if(g_silent_mode) {
        POINT_COLOR = RED; 
        LCD_ShowString(UI_BEEP_X, UI_BEEP_Y, 200, 24, 24, (u8*)"[ Mode: MUTE ] ");
    } else {
        POINT_COLOR = GREEN; 
        LCD_ShowString(UI_BEEP_X, UI_BEEP_Y, 200, 24, 24, (u8*)"[ Mode: SOUND ]");
    }

    // 4. 阈值设置显示
    BACK_COLOR = WHITE; 
    POINT_COLOR = BLACK;
    LCD_ShowString(SETTING_TITLE_X, SETTING_TITLE_Y, 200, 16, 16, (u8*)"-- SYSTEM SETTINGS --");
    
    u16 y = SET_START_Y;
    
    // 阈值显示宏(当前设置项标红并加<<标识)
    #define SHOW_PARAM(param_enum, name, val, col_x) \
        POINT_COLOR = (g_is_setting_mode && g_current_param == param_enum) ? RED : BLACK; \
        sprintf(buf, "%s: %d%s", name, val, (g_is_setting_mode && g_current_param == param_enum) ? " <<" : "   "); \
        LCD_ShowString(col_x, y, 220, 16, 16, (u8*)buf);

    // 显示各阈值
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

/**
 * @brief  按键处理函数
 * @note   处理KEY0/KEY1/WK_UP/KEY2按键事件
 * @retval 无
 */
void Key_Process(void)
{
    u8 key = KEY_Scan(0); // 扫描按键
    
    if (key == KEY0_PRES) { // KEY0: 设置模式切换/参数切换
        if (!g_is_setting_mode) {
            g_is_setting_mode = 1;        // 进入设置模式
            g_current_param = PARAM_TEMP_H; // 默认选中温度上限
        } else {
            g_current_param++;            // 切换到下一个参数
            if (g_current_param > PARAM_PM25_H) {
                g_is_setting_mode = 0;    // 退出设置模式
            }
        }
    } 
    else if (key == KEY1_PRES) { // KEY1: 参数+ / 灯光模式切换
        if (g_is_setting_mode) {
            // 设置模式:参数增加(带边界检查)
            switch(g_current_param) {
                case PARAM_TEMP_H: if(temp_H<99) temp_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H); break;
                case PARAM_TEMP_L: if(temp_L<temp_H) temp_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L); break;
                case PARAM_HUMI_H: if(humi_H<100) humi_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H); break;
                case PARAM_HUMI_L: if(humi_L<humi_H) humi_L++; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L); break;
                case PARAM_PM25_H: if(pm25_H<999) pm25_H++; AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H); break;
            }
        } else {
            // 正常模式:切换灯光模式
            g_light_mode++;
            if (g_light_mode > MODE_OFF) g_light_mode = MODE_AUTO;
        }
    } 
    else if (key == WKUP_PRES) { // WK_UP: 参数- / 静音切换
        if (g_is_setting_mode) {
            // 设置模式:参数减少(带边界检查)
             switch(g_current_param) {
                case PARAM_TEMP_H: if(temp_H>temp_L) temp_H--; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H); break;
                case PARAM_TEMP_L: if(temp_L>0) temp_L--; AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L); break;
                case PARAM_HUMI_H: if(humi_H>humi_L) humi_H--; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_H, humi_H); break;
                case PARAM_HUMI_L: if(humi_L>0) humi_L--; AT24CXX_WriteOneByte(EEPROM_ADDR_HUMI_L, humi_L); break;
                case PARAM_PM25_H: if(pm25_H>0) pm25_H--; AT24CXX_WriteOneByte(EEPROM_ADDR_PM25_H, pm25_H); break;
            }
        } else {
            // 正常模式:切换静音模式
            g_silent_mode = !g_silent_mode;
        }
    }
    // KEY2: 控制LCD背光
    else if (key == KEY2_PRES) { 
        LCD_LED = !LCD_LED; 
    }
}

/**
 * @brief  报警逻辑处理
 * @note   控制蜂鸣器和WS2812灯带的报警效果
 * @retval 无
 */
void Alarm_Update(void)
{
    static u16 tick = 0;                // 报警计数器
    static u16 beep_duration_cnt = 0;   // 蜂鸣器持续计数器
    u8 is_any_alarm = 0;                // 任意报警标志
    u8 enable_sound = 0;                // 蜂鸣器使能标志

    tick++;

    // 判断是否有报警
    if (g_ai_alarm || g_security_alarm || g_pm_alarm || g_temp_alarm || g_humi_alarm) {
        is_any_alarm = 1;
    }

    // 报警持续计数
    if (is_any_alarm) {
        if (beep_duration_cnt < 65000) beep_duration_cnt++;
    } else {
        beep_duration_cnt = 0; // 无报警重置计数器
    }

    // 蜂鸣器控制(前5秒响,之后停止)
    if (!g_silent_mode && beep_duration_cnt < 50) {
        enable_sound = 1;
    } else {
        enable_sound = 0; 
    }
    
    BEEP = 0; // 默认关闭蜂鸣器

    // 设置模式:灯带呼吸效果
    if (g_is_setting_mode) { 
        if(tick % 2 == 0) {
            WS2812_Set_All_Color(0,0,0);
            WS2812_Set_Pixel_Color( (tick/2) % LED_NUM, 0, 50, 50); // 蓝色呼吸
            WS2812_Refresh();
        }
        return;
    }

    // 应急模式:灯带常亮白色
    if (g_light_mode == MODE_EMERGENCY) {
        WS2812_Set_All_Color(255, 255, 255); 
        WS2812_Refresh();
        return; 
    }

    // 各报警类型的灯光和蜂鸣器效果
    if (g_ai_alarm) { // 火灾报警:红色快闪+蜂鸣器快响
        if (tick % 1 == 0) { 
            if(enable_sound) BEEP = !BEEP; 
            WS2812_Set_All_Color(255, 0, 0); 
            WS2812_Refresh(); 
        } else { WS2812_Clear(); }
    } 
    else if (g_security_alarm) { // 入侵报警:品红色慢闪+蜂鸣器慢响
        if (tick % 5 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(128, 0, 128); 
            WS2812_Refresh(); 
        } else if (tick % 5 == 1) WS2812_Clear();
    }
    else if (g_pm_alarm) { // PM2.5报警:橙色中速闪+蜂鸣器中速响
        if (tick % 2 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(200, 60, 0); 
            WS2812_Refresh(); 
        } else WS2812_Clear();
    }
    else if (g_temp_alarm) { // 温度报警:黄色慢闪+蜂鸣器慢响
        if (tick % 5 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(200, 100, 0); 
            WS2812_Refresh(); 
        } else if (tick % 5 == 1) WS2812_Clear();
    }
    else if (g_humi_alarm) { // 湿度报警:蓝色超慢闪+蜂鸣器超慢响
        if (tick % 10 == 0) { 
            if(enable_sound) BEEP = !BEEP;
            WS2812_Set_All_Color(0, 0, 200); 
            WS2812_Refresh(); 
        } else if (tick % 10 == 1) WS2812_Clear();
    } 
    else { // 正常状态
        if (g_light_mode == MODE_AUTO) {
            WS2812_Set_All_Color(0, 50, 0); // 自动模式:绿色常亮
            WS2812_Refresh();
        } else {
            WS2812_Clear(); // 关闭模式:熄灭
        }
    }
}

// 串口数据上报函数 (每1秒调用一次)
void Serial_Data_Report(u8 temp, u8 humi, u16 pm2_5)
{
    // 定义故障代码，参考通信协议 E:YY
    u8 error_code = 0;
    if(g_err_dht11) error_code = 1;
    else if(g_err_pms) error_code = 2;
    else if(g_err_sd) error_code = 3;

    // 格式: $D:TXX.X,HXX.X,PXXX,E:YY!
    // 这里使用 u_printf (如果您的 usart.c 支持格式化输出) 或直接使用标准 printf
    // 假设您的 usart.c 支持 printf 重定向
    printf("$D:T%d,H%d,P%d,E:%02d!\r\n", 
           temp, 
           humi, 
           pm2_5, 
           error_code);
           
    // 上报阈值信息 (可选，但为了完整性建议加上)
    printf("$THH:%02d,TLL:%02d,HHH:%02d,HLL:%02d,PMH:%03d!\r\n",
           temp_H, temp_L, humi_H, humi_L, pm25_H);
}
// 串口指令解析函数：实现上位机远程修改阈值和时间
void USART_Process_Command(u8 *Rx_Buf, u16 Rx_Status)
{
    u8 *p = Rx_Buf;
    u32 temp_val;
    u16 len;
    
    // 检查是否接收到完整指令 (0x8000 标志完整接收)
    if(Rx_Status & 0x8000)
    {
        len = Rx_Status & 0x3FFF;
        Rx_Buf[len] = '\0'; // 确保字符串有效结束
        
        // 确保指令以 '$' 开头，以 '!' 结尾
        if(p[0] == '$' && p[len-1] == '!')
        {
            // --- 1. 设置 RTC 时间指令 ($T:HHMMSS!) ---
            // 假设 RTC_Set(u8 hour, u8 min, u8 sec) 存在
            if(strstr((const char*)p, "$T:") == (const char*)p)
            {
                u32 time_data; // HHMMSS
                if(sscanf((const char*)p, "$T:%06d!", (int*)&time_data) == 1) 
                {
                    // 提取时、分、秒
                    u8 hour = (u8)(time_data / 10000);
                    u8 min = (u8)((time_data / 100) % 100);
                    u8 sec = (u8)(time_data % 100);
                    
                    if(hour < 24 && min < 60 && sec < 60)
                    {
                        RTC_Set(calendar.w_year, calendar.w_month, calendar.w_date, hour, min, sec); 
                        printf("[CMD] Set Time OK: %02d:%02d:%02d\r\n", hour, min, sec);
                    }
                }
            }
            
            // --- 2. 设置阈值指令 (THH, TLL, HHH, HLL, PMH) ---
            
            // 设置温度上限 $THH:XX!
            else if(strstr((const char*)p, "$THH:") == (const char*)p && sscanf((const char*)p, "$THH:%d!", (int*)&temp_val) == 1) 
            {
                if (temp_val < 99 && temp_val > temp_L) {
                    temp_H = (u16)temp_val;
                    AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_H, temp_H);
                    printf("[CMD] Set Temp H OK: %d\r\n", temp_H);
                }
            }
            // 设置温度下限 $TLL:XX!
            else if(strstr((const char*)p, "$TLL:") == (const char*)p && sscanf((const char*)p, "$TLL:%d!", (int*)&temp_val) == 1) 
            {
                if (temp_val > 0 && temp_val < temp_H) {
                    temp_L = (u16)temp_val;
                    AT24CXX_WriteOneByte(EEPROM_ADDR_TEMP_L, temp_L);
                    printf("[CMD] Set Temp L OK: %d\r\n", temp_L);
                }
            }
            // --- 请自行补充 HHH, HLL, PMH 的解析逻辑 ---
            /*
            // 示例：设置湿度上限 $HHH:XX!
            else if(strstr((const char*)p, "$HHH:") == (const char*)p && sscanf((const char*)p, "$HHH:%d!", (int*)&temp_val) == 1) 
            {
                // ... 湿度阈值逻辑 ...
            }
            */

            // --- 3. 静音控制 $MUTE:X! ---
            else if(strstr((const char*)p, "$MUTE:") == (const char*)p && sscanf((const char*)p, "$MUTE:%d!", (int*)&temp_val) == 1) 
            {
                if (temp_val == 0 || temp_val == 1) {
                    g_silent_mode = (u8)temp_val;
                    printf("[CMD] Set Mute Mode: %d\r\n", g_silent_mode);
                }
            }
        }
        
        // 解析完毕，清除状态标志
        USART_RX_STA = 0; 
    }
}
