#ifndef AI_MODEL_H
#define AI_MODEL_H

#include "sys.h" // 包含您工程的基础类型定义

/**
 * @brief  初始化AI模型 (此模型无需额外初始化，保留为空)
 */
void ai_model_init(void);

/**
 * @brief  使用隔离森林模型预测一个数据点是否为异常
 * @param  features: 一个包含4个float类型特征的数组，顺序必须是：
 * [temperature, humidity, particles_0_3um, particles_2_5um]
 * @retval int8_t: 返回 1 代表 "正常 (Inlier)"
 * 返回 -1 代表 "异常 (Anomaly)"
 */
int8_t ai_model_predict(float* features);

#endif // AI_MODEL_H
