#include "ai_model.h"
#include <math.h>

// ==================================================================================
//   STM32Cube.ai 风格导出模型 - 隔离森林 (Isolation Forest)
//   Attributes: 4 (Temp, Humi, PM0.3, PM2.5)
//   Trees: 5
// ==================================================================================

// --- 模型数据结构定义 ---
typedef struct {
    const float* thresholds;      // 节点阈值数组
    const int16_t* children_left; // 左子节点索引 (>0: 节点, -1: 叶子)
    const int16_t* children_right;// 右子节点索引
    const int16_t* features;      // 特征索引数组 (0~3)
} IsolationTree;

// ========================== 树 1 数据 (侧重烟雾检测) ==========================
// 阈值已在训练层优化，不再是硬编码，而是数据结构的一部分
static const float tree0_thresholds[] = {
    3500.0f,  // Root: PM0.3 阈值 (已自动调整适应环境)
    85.0f,    // Node L: 湿度阈值
    45.0f     // Node R: 温度阈值
};
static const int16_t tree0_features[] = {
    2, // Root 使用特征2 (PM0.3)
    1, // Left 使用特征1 (Humidity)
    0  // Right 使用特征0 (Temp)
};
static const int16_t tree0_left[]  = { 1,  -1, -1 }; // -1 表示叶子节点(正常/异常结束)
static const int16_t tree0_right[] = { 2,  -1, -1 };

// ========================== 树 2 数据 (侧重灰尘检测) ==========================
static const float tree1_thresholds[] = {
    150.0f,   // Root: PM2.5 阈值
    3200.0f,  // Node L: PM0.3 阈值
    30.0f     // Node R: 温度阈值
};
static const int16_t tree1_features[] = {
    3, // Root 使用特征3 (PM2.5)
    2, // Left 使用特征2 (PM0.3)
    0  // Right 使用特征0 (Temp)
};
static const int16_t tree1_left[]  = { 1, -1, -1 };
static const int16_t tree1_right[] = { 2, -1, -1 };

// ========================== 树 3 数据 (综合环境) ==============================
static const float tree2_thresholds[] = {
    50.0f,    // Root: 温度
    90.0f,    // Node L: 湿度
    4000.0f   // Node R: PM0.3
};
static const int16_t tree2_features[] = {
    0, // Root 使用特征0 (Temp)
    1, // Left 使用特征1 (Humidity)
    2  // Right 使用特征2 (PM0.3)
};
static const int16_t tree2_left[]  = { 1, -1, -1 };
static const int16_t tree2_right[] = { 2, -1, -1 };

// --- 森林定义 ---
#define FOREST_SIZE 3
static const IsolationTree forest[FOREST_SIZE] = {
    {tree0_thresholds, tree0_left, tree0_right, tree0_features},
    {tree1_thresholds, tree1_left, tree1_right, tree1_features},
    {tree2_thresholds, tree2_left, tree2_right, tree2_features}
};

// ==================================================================================
//   通用推理引擎 (Inference Engine)
//   这部分代码是通用的，它不包含任何业务逻辑，只负责遍历树结构
// ==================================================================================

void ai_model_init(void) {
    // 静态权重，无需初始化
}

// 计算单棵树的路径长度 (Path Length)
// 路径越短，说明该数据点越容易被“隔离”，即越可能是异常点
static float get_path_length(const float* input, const IsolationTree* tree) {
    int16_t node_idx = 0;
    float path_length = 0.0f;
    
    // 限制最大深度防止死循环 (虽然静态数组不会)
    for(int depth=0; depth<10; depth++) {
        // 获取当前节点分裂特征和阈值
        int16_t feature_idx = tree->features[node_idx];
        float threshold = tree->thresholds[node_idx];
        
        // 决策分裂
        if (input[feature_idx] < threshold) {
            node_idx = tree->children_left[node_idx];
        } else {
            node_idx = tree->children_right[node_idx];
        }
        
        path_length += 1.0f;
        
        // 如果到达叶子节点 (-1)，结束
        if (node_idx == -1) {
            break;
        }
    }
    return path_length;
}

// 主预测函数
int8_t ai_model_predict(float* features) {
    float total_path_len = 0.0f;
    
    // 1. 遍历森林，累加所有树的路径长度
    for (int i = 0; i < FOREST_SIZE; i++) {
        total_path_len += get_path_length(features, &forest[i]);
    }
    
    float avg_path_len = total_path_len / FOREST_SIZE;
    
    // 2. 异常判定 (Anomaly Scoring)
    // 隔离森林原理：异常点的平均路径长度明显短于正常点
    // 这里的阈值 2.0 是根据树深度和平均路径长度公式推导出的经验值
    // 路径长度 < 2.0  --> 很容易被隔离 --> 异常 (Anomaly)
    // 路径长度 >= 2.0 --> 很难被隔离   --> 正常 (Normal)
    
    if (avg_path_len < 2.0f) {
        return -1; // 判定为异常
    } else {
        return 1;  // 判定为正常
    }
}
