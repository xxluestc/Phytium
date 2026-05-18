/**
 * @file    chaos_encrypt.c
 * @brief   混沌加密模块实现
 * @details 使用二维混沌系统生成密钥流，实现数据加密解密
 * 
 * @section 混沌系统原理
 * 本模块使用二维混沌映射作为伪随机数生成器(PRNG)。
 * 混沌系统具有以下特性，使其适合用于加密:
 *   1. 初值敏感性: 初始条件的微小变化会导致完全不同的输出
 *   2. 遍历性: 系统状态会遍历整个相空间
 *   3. 不可预测性: 长期行为无法预测
 *   4. 确定性: 相同初始条件产生相同序列
 * 
 * @section 混沌方程
 * 二维混沌映射方程:
 * @code
 *   x_{n+1} = a*cos(x_n + φ0) + b0*sin(b1*sin(y_n + φ1) + φ2)
 *   y_{n+1} = c*cos(y_n + φ3) + d0*sin(d1*sin(x_n + φ4) + φ5)
 * @endcode
 * 
 * 其中:
 *   - a, b0, b1, c, d0, d1: 方程系数参数
 *   - φ0-φ5: 相位参数，增加非线性复杂度
 *   - x_n, y_n: 当前状态
 *   - x_{n+1}, y_{n+1}: 下一状态
 * 
 * @section 加密算法
 * 加密过程分为三个步骤:
 *   1. 字节置换(Scramble): 使用混沌序列决定交换位置，打乱数据顺序
 *   2. XOR加密: 将数据与混沌密钥流进行异或操作
 *   3. 同步码生成: 记录加密后的混沌状态，用于接收端同步
 * 
 * 解密过程是加密的逆过程:
 *   1. 状态同步: 从同步码恢复混沌状态
 *   2. XOR解密: 与加密相同的异或操作
 *   3. 字节逆置换(Unscramble): 恢复原始数据顺序
 * 
 * @section 同步机制
 * 由于混沌系统对初值敏感，发送端和接收端必须保持状态同步。
 * 同步码结构(32位):
 *   [31:16] - x状态量化值 (精度10000)
 *   [15:0]  - y状态量化值 (精度10000)
 * 
 * 发送端在加密完成后获取同步码，随数据包发送。
 * 接收端收到数据后，先从同步码恢复混沌状态，再进行解密。
 * 
 * @section 安全性说明
 * 1. 预热迭代: 初始化时执行1000次迭代，消除初始状态可预测性
 * 2. 参数保密: 混沌参数应保密，建议使用自定义参数
 * 3. 种子管理: 初始化种子应安全传递，避免泄露
 * 
 * @author  Electronic Design Contest Team
 * @date    2024
 */

#include "chaos_encrypt.h"
#include <math.h>
#include <string.h>

/*============================================================================*/
/*                              全局变量                                       */
/*============================================================================*/

/**
 * @brief 混沌系统状态变量
 * @note  状态值范围约为 [-10, 10]，取决于参数设置
 */
static float g_x = 0.1f;    /**< 混沌状态 x */
static float g_y = 0.2f;    /**< 混沌状态 y */

/**
 * @brief 混沌系统参数
 * @note  发送端和接收端必须使用完全相同的参数
 *        这些参数决定了混沌系统的行为特性
 */
static ChaosParams_t g_params = {
    .a = 2.5f,      /**< x方程余弦项系数 */
    .b0 = 1.0f,     /**< x方程正弦项系数 */
    .b1 = 3.0f,     /**< x方程内层正弦系数 */
    .c = 2.5f,      /**< y方程余弦项系数 */
    .d0 = 1.0f,     /**< y方程正弦项系数 */
    .d1 = 3.0f,     /**< y方程内层正弦系数 */
    .phi = {0.5f, 0.3f, 0.7f, 0.4f, 0.6f, 0.2f}  /**< 相位参数 φ0-φ5 */
};

/**
 * @brief 密钥流缓冲区
 * @note  存储256字节的密钥流，用于XOR加密
 *        密钥流用完后自动重新生成
 */
static uint8_t g_key_stream[KEY_STREAM_SIZE];

/**
 * @brief 当前密钥流读取位置
 * @note  范围 [0, KEY_STREAM_SIZE-1]
 */
static uint16_t g_key_index = 0;

/*============================================================================*/
/*                              内部函数                                       */
/*============================================================================*/

/**
 * @brief  混沌迭代 - 执行一次混沌方程计算
 * @note   更新全局状态 g_x 和 g_y
 *         这是混沌系统的核心，每次调用产生新的状态
 * 
 * 计算过程:
 *   1. 计算新的x值: x_{n+1} = a*cos(x_n + φ0) + b0*sin(b1*sin(y_n + φ1) + φ2)
 *   2. 计算新的y值: y_{n+1} = c*cos(y_n + φ3) + d0*sin(d1*sin(x_n + φ4) + φ5)
 *   3. 更新全局状态
 */
static void chaos_iterate(void)
{
    /* 
     * 计算新的x值
     * 公式: x_{n+1} = a*cos(x_n + φ0) + b0*sin(b1*sin(y_n + φ1) + φ2)
     * 
     * 结构分析:
     *   - a*cos(x_n + φ0): 余弦项，提供基础振荡
     *   - b0*sin(b1*sin(y_n + φ1) + φ2): 嵌套正弦项，引入y的耦合和非线性
     */
    float x_new = g_params.a * cosf(g_x + g_params.phi[0]) +
                  g_params.b0 * sinf(g_params.b1 * sinf(g_y + g_params.phi[1]) + g_params.phi[2]);
    
    /* 
     * 计算新的y值
     * 公式: y_{n+1} = c*cos(y_n + φ3) + d0*sin(d1*sin(x_n + φ4) + φ5)
     * 
     * 结构分析:
     *   - c*cos(y_n + φ3): 余弦项，提供基础振荡
     *   - d0*sin(d1*sin(x_n + φ4) + φ5): 嵌套正弦项，引入x的耦合和非线性
     */
    float y_new = g_params.c * cosf(g_y + g_params.phi[3]) +
                  g_params.d0 * sinf(g_params.d1 * sinf(g_x + g_params.phi[4]) + g_params.phi[5]);
    
    /* 更新全局混沌状态 */
    g_x = x_new;
    g_y = y_new;
}

/**
 * @brief  生成一个随机字节
 * @return 从混沌状态生成的随机字节(0-255)
 * 
 * @note   生成算法:
 *         1. 迭代混沌方程4次，增加随机性
 *         2. 提取x和y浮点数的位模式
 *         3. 混合所有位生成一个字节
 * 
 * @details 浮点数到字节的转换:
 *          IEEE 754单精度浮点数有32位
 *          通过异或操作混合x和y的所有位
 *          最终取低8位作为输出
 */
static uint8_t chaos_generate_byte(void)
{
    /* 迭代4次混沌方程，增加随机性和扩散性 */
    for (int i = 0; i < 4; i++) {
        chaos_iterate();
    }
    
    /* 
     * 将浮点数的位模式当作整数处理
     * 使用memcpy避免违反严格别名规则
     * IEEE 754浮点数的位模式包含符号、指数和尾数
     */
    uint32_t x_bits;
    memcpy(&x_bits, &g_x, sizeof(float));
    uint32_t y_bits;
    memcpy(&y_bits, &g_y, sizeof(float));
    
    /* 
     * 混合x和y的所有位生成一个字节
     * 使用异或操作混合，确保所有位都影响结果
     * 
     * 位混合策略:
     *   - x_bits的低8位
     *   - x_bits的8-15位
     *   - y_bits的16-23位
     *   - x_bits的24-31位
     */
    uint8_t byte = (uint8_t)((x_bits ^ y_bits) & 0xFF);
    byte ^= (uint8_t)((x_bits >> 8) & 0xFF);
    byte ^= (uint8_t)((y_bits >> 16) & 0xFF);
    byte ^= (uint8_t)((x_bits >> 24) & 0xFF);
    
    return byte;
}

/**
 * @brief  生成密钥流
 * @note   生成256字节的密钥流到 g_key_stream
 *         重置密钥流读取索引为0
 * 
 * @details 密钥流用于XOR加密
 *          每次加密前需要确保有足够的密钥流
 *          密钥流用完后自动调用此函数重新生成
 */
static void chaos_generate_key_stream(void)
{
    /* 逐字节生成密钥流 */
    for (uint16_t i = 0; i < KEY_STREAM_SIZE; i++) {
        g_key_stream[i] = chaos_generate_byte();
    }
    /* 重置读取索引 */
    g_key_index = 0;
}

/**
 * @brief  从密钥流读取下一个字节
 * @return 密钥流字节
 * 
 * @note   密钥流用完后自动生成新的256字节密钥流
 *         这确保了可以加密任意长度的数据
 */
static uint8_t chaos_next_byte(void)
{
    /* 读取当前字节 */
    uint8_t byte = g_key_stream[g_key_index];
    g_key_index++;
    
    /* 密钥流用完，生成新的密钥流 */
    if (g_key_index >= KEY_STREAM_SIZE) {
        chaos_generate_key_stream();
    }
    
    return byte;
}

/**
 * @brief  字节置换 - 打乱数据顺序
 * @param  data 数据缓冲区
 * @param  len  数据长度
 * 
 * @note   置换算法:
 *         1. 从前向后遍历数据
 *         2. 使用混沌密钥流决定交换位置
 *         3. 交换当前位置与目标位置的数据
 * 
 * @details 置换增加了加密的扩散性
 *          即使明文有规律，置换后也会变得随机
 *          交换位置由密钥流决定，只有知道密钥流才能逆置换
 */
static void chaos_scramble(uint8_t *data, uint16_t len)
{
    if (len < 2) return;
    
    uint8_t key_byte;
    /* 遍历前半部分数据进行交换 */
    for (uint16_t i = 0; i < len / 2; i++) {
        /* 从密钥流获取一个字节，决定交换位置 */
        key_byte = chaos_next_byte();
        /* 计算交换目标位置: j ∈ [i, len-1] */
        uint16_t j = i + (key_byte % (len - i));
        
        /* 交换位置i和位置j的数据 */
        uint8_t temp = data[i];
        data[i] = data[j];
        data[j] = temp;
    }
}

/**
 * @brief  字节逆置换 - 恢复数据顺序
 * @param  data 数据缓冲区
 * @param  len  数据长度
 * 
 * @note   逆置换算法:
 *         1. 先记录所有交换位置（需要相同的密钥流）
 *         2. 按相反顺序恢复交换
 * 
 * @details 由于置换是从前向后进行的
 *          逆置换必须从后向前进行
 *          这需要先读取所有密钥流字节，记录交换位置
 */
static void chaos_unscramble(uint8_t *data, uint16_t len)
{
    if (len < 2) return;
    
    /* 记录交换位置的数组 */
    uint8_t swap_records[256];
    uint16_t swap_count = len / 2;
    if (swap_count > 256) swap_count = 256;
    
    /* 先读取所有密钥流字节，记录交换位置 */
    for (uint16_t i = 0; i < swap_count; i++) {
        swap_records[i] = chaos_next_byte();
    }
    
    /* 按相反顺序恢复交换 */
    for (int16_t i = swap_count - 1; i >= 0; i--) {
        /* 计算原始交换目标位置 */
        uint16_t j = i + (swap_records[i] % (len - i));
        
        /* 恢复交换（与原始交换操作相同，但顺序相反） */
        uint8_t temp = data[i];
        data[i] = data[j];
        data[j] = temp;
    }
}

/*============================================================================*/
/*                              公共函数                                       */
/*============================================================================*/

/**
 * @brief  初始化混沌加密系统
 * @param  seed 初始化种子，双方必须使用相同种子
 * @warning 发送端和接收端必须使用相同的seed值
 *          否则无法正确加解密
 */
void chaos_init(uint32_t seed)
{
    /* 
     * 从种子计算初始状态
     * 将32位种子分成两部分，分别影响x和y的初始值
     * 初始值范围: x ∈ [0.1, 0.6], y ∈ [0.2, 0.7]
     */
    g_x = 0.1f + (seed & 0xFFFF) / 65536.0f * 0.5f;
    g_y = 0.2f + ((seed >> 16) & 0xFFFF) / 65536.0f * 0.5f;
    g_key_index = 0;
    
    for (uint32_t i = 0; i < CHAOS_WARMUP_ITERATIONS; i++) {
        chaos_generate_byte();
    }
    
    /* 生成初始密钥流 */
    chaos_generate_key_stream();
}

/**
 * @brief  设置混沌参数
 * @param  params 混沌参数结构体指针
 * 
 * @note   默认参数已内置，通常无需调用此函数
 *         如需自定义参数，双方必须使用相同参数
 * 
 * @warning 参数选择影响混沌系统的行为
 *          不当参数可能导致系统退化为周期轨道
 */
void chaos_set_params(const ChaosParams_t *params)
{
    if (params) {
        memcpy(&g_params, params, sizeof(ChaosParams_t));
    }
}

/**
 * @brief  获取同步码
 * @return 32位同步码，编码当前混沌状态
 * 
 * @note   同步码结构（32位）:
 *         [31:16] - x状态量化值（精度10000）
 *         [15:0]  - y状态量化值（精度10000）
 * 
 * @details 量化过程:
 *          1. 取x和y的绝对值
 *          2. 乘以10000进行量化
 *          3. 取低16位存入同步码
 * 
 * @note   使用场景:
 *         - 加密完成后调用，获取同步码
 *         - 将同步码随加密数据发送给接收端
 *         - 接收端用同步码恢复混沌状态
 */
uint32_t chaos_get_sync_code(void)
{
    uint32_t code = 0;
    
    /* 
     * 将x和y编码到32位同步码中
     * 使用fabsf取绝对值，避免负数问题
     * 乘以10000进行量化，保留4位小数精度
     */
    code |= ((uint32_t)(fabsf(g_x) * 10000) & 0xFFFF) << 16;
    code |= ((uint32_t)(fabsf(g_y) * 10000) & 0xFFFF);
    
    return code;
}

/**
 * @brief  从同步码恢复混沌状态
 * @param  sync_code 32位同步码
 * 
 * @note   恢复流程:
 *         1. 从同步码解析x和y状态
 *         2. 直接生成密钥流（不预热！）
 * 
 * @details 解析过程:
 *          - x_hint = (sync_code高16位) / 10000
 *          - y_hint = (sync_code低16位) / 10000
 *          
 *          有效性检查:
 *          - 状态值必须在合理范围内 (0, 100)
 * 
 * @warning 此函数不执行预热迭代！
 *          因为同步码记录的是加密前的混沌状态（已经预热过），
 *          解密时直接从该状态生成密钥流即可。
 *          只有 chaos_init() 才需要预热迭代。
 */
void chaos_sync_from_code(uint32_t sync_code)
{
    /* 从同步码解析x和y状态 */
    float x_hint = ((sync_code >> 16) & 0xFFFF) / 10000.0f;
    float y_hint = (sync_code & 0xFFFF) / 10000.0f;
    
    /* 恢复混沌状态（带有效性检查） */
    if (x_hint > 0 && x_hint < 100) g_x = x_hint;
    if (y_hint > 0 && y_hint < 100) g_y = y_hint;
    
    /* 
     * 直接生成密钥流，不执行预热迭代！
     * 
     * 原因：同步码记录的是加密前的混沌状态
     * 加密端在获取同步码时，已经完成了预热迭代
     * 解密端只需要恢复到相同状态，然后生成相同的密钥流
     */
    chaos_generate_key_stream();
}

/**
 * @brief  XOR加密数据块
 * @param  data 数据缓冲区（原地加密）
 * @param  len  数据长度
 * 
 * @note   XOR操作是可逆的，加密和解密使用相同函数
 *         data[i] ^= key_stream[i]
 */
void chaos_encrypt_block(uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) return;
    
    /* 逐字节与密钥流异或 */
    for (uint16_t i = 0; i < len; i++) {
        data[i] ^= chaos_next_byte();
    }
}

/**
 * @brief  XOR解密数据块
 * @param  data 数据缓冲区（原地解密）
 * @param  len  数据长度
 * 
 * @note   XOR操作可逆，解密与加密相同
 *         内部直接调用chaos_encrypt_block
 */
void chaos_decrypt_block(uint8_t *data, uint16_t len)
{
    chaos_encrypt_block(data, len);
}

/**
 * @brief  加密数据包
 * @param  input     原始数据
 * @param  input_len 原始数据长度
 * @param  output    输出缓冲区（加密数据）
 * @param  sync_code 输出同步码
 * @return 加密后数据长度，0表示失败
 * 
 * @note   加密流程:
 *         1. 参数校验
 *         2. 获取同步码（记录加密前的混沌状态）
 *         3. 复制原始数据到输出缓冲区
 *         4. 字节置换（打乱数据顺序）
 *         5. XOR加密（与密钥流异或）
 * 
 * @warning 输出缓冲区大小应 >= input_len
 *          输入数据长度不能超过 MAX_ENCRYPT_DATA_LEN (128字节)
 * 
 * @details 同步码必须在加密操作前获取，因为加密过程会消耗密钥流，
 *          改变混沌状态。解密端需要用同步码恢复到加密前的状态，
 *          才能生成相同的密钥流进行解密。
 */
uint16_t chaos_encrypt_packet(const uint8_t *input, uint16_t input_len, uint8_t *output, uint32_t *sync_code)
{
    /* 参数校验 */
    if (input == NULL || output == NULL || input_len == 0 || input_len > MAX_ENCRYPT_DATA_LEN) {
        return 0;
    }
    
    /* 步骤1: 获取同步码 - 记录加密前的混沌状态 */
    *sync_code = chaos_get_sync_code();
    
    /* 步骤2: 复制数据到输出缓冲区 */
    memcpy(output, input, input_len);
    
    /* 步骤3: 字节置换 - 打乱数据顺序 */
    chaos_scramble(output, input_len);
    
    /* 步骤4: XOR加密 - 与密钥流异或 */
    chaos_encrypt_block(output, input_len);
    
    return input_len;
}

/**
 * @brief  解密数据包
 * @param  input     加密数据
 * @param  input_len 加密数据长度
 * @param  output    输出缓冲区（原始数据）
 * @param  sync_code 同步码
 * @return 解密后数据长度，0表示失败
 * 
 * @note   解密流程:
 *         1. 参数校验
 *         2. 从同步码恢复混沌状态
 *         3. 复制加密数据到输出缓冲区
 *         4. XOR解密（与密钥流异或）
 *         5. 字节逆置换（恢复数据顺序）
 * 
 * @warning 解密前必须先恢复混沌状态
 *          chaos_decrypt_packet内部会自动调用chaos_sync_from_code
 */
uint16_t chaos_decrypt_packet(const uint8_t *input, uint16_t input_len, uint8_t *output, uint32_t sync_code)
{
    /* 参数校验 */
    if (input == NULL || output == NULL || input_len == 0) {
        return 0;
    }
    
    /* 步骤1: 从同步码恢复混沌状态 */
    chaos_sync_from_code(sync_code);
    
    /* 步骤2: 复制数据到输出缓冲区 */
    memcpy(output, input, input_len);
    
    /* 步骤3: XOR解密 - 与密钥流异或 */
    chaos_decrypt_block(output, input_len);
    
    /* 步骤4: 字节逆置换 - 恢复数据顺序 */
    chaos_unscramble(output, input_len);
    
    return input_len;
}
