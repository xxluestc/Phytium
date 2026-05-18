#ifndef __CHAOS_ENCRYPT_H
#define __CHAOS_ENCRYPT_H

#include <stdint.h>
#define CHAOS_WARMUP_ITERATIONS  1000
#define KEY_STREAM_SIZE          256
#define MAX_ENCRYPT_DATA_LEN     128

/** @} */

/*============================================================================*/
/*                              数据结构                                       */
/*============================================================================*/

/**
 * @brief 混沌系统参数结构体
 * @details 包含混沌方程的所有参数，发送端和接收端必须使用相同参数
 * 
 * 混沌方程:
 *   x_{n+1} = a*cos(x_n + φ0) + b0*sin(b1*sin(y_n + φ1) + φ2)
 *   y_{n+1} = c*cos(y_n + φ3) + d0*sin(d1*sin(x_n + φ4) + φ5)
 * 
 * 参数说明:
 *   - a, b0, b1: x方程参数，控制x方向的混沌行为
 *   - c, d0, d1: y方程参数，控制y方向的混沌行为
 *   - phi[0-5]: 相位参数，增加系统复杂度
 * 
 * 默认参数值:
 *   a=2.5, b0=1.0, b1=3.0, c=2.5, d0=1.0, d1=3.0
 *   phi={0.5, 0.3, 0.7, 0.4, 0.6, 0.2}
 */
typedef struct {
    float a;        /**< x方程余弦项系数 */
    float b0;       /**< x方程正弦项系数 */
    float b1;       /**< x方程内层正弦系数 */
    float c;        /**< y方程余弦项系数 */
    float d0;       /**< y方程正弦项系数 */
    float d1;       /**< y方程内层正弦系数 */
    float phi[6];   /**< 相位参数 φ0-φ5 */
} ChaosParams_t;

void chaos_init(uint32_t seed);
void chaos_set_params(const ChaosParams_t *params);

uint32_t chaos_get_sync_code(void);
/**
 * @brief  从同步码恢复混沌状态
 * @param  sync_code 32位同步码
 * 
 * @note   恢复流程:
 *         1. 从同步码解析x和y状态
 *         2. 直接生成密钥流（不预热！）
 * 
 * @note   使用场景:
 *         - 接收端收到数据包后，先提取同步码
 *         - 调用此函数恢复与发送端相同的混沌状态
 *         - 然后进行解密操作
 * 
 * @warning 此函数不执行预热迭代！
 *          因为同步码记录的是加密前的混沌状态（已经预热过），
 *          解密时直接从该状态生成密钥流即可。
 *          只有 chaos_init() 才需要预热迭代。
 */
void chaos_sync_from_code(uint32_t sync_code);

uint16_t chaos_encrypt_packet(const uint8_t *input, uint16_t input_len, 
                               uint8_t *output, uint32_t *sync_code);
uint16_t chaos_decrypt_packet(const uint8_t *input, uint16_t input_len, 
                               uint8_t *output, uint32_t sync_code);

void chaos_encrypt_block(uint8_t *data, uint16_t len);
void chaos_decrypt_block(uint8_t *data, uint16_t len);

#endif /* __CHAOS_ENCRYPT_H */
