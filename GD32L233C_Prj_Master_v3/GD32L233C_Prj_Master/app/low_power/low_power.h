#ifndef __LOW_POWER_H
#define __LOW_POWER_H

#include "gd32l23x.h"

/* PMU深度睡眠模式配置 */
#define PMU_DEEPSLEEP_ENTRY                WFI_CMD    // 使用WFI指令进入深度睡眠
#define DEEPSLEEP_WAKEUP_INTERVAL_MS       10         // 深度睡眠最大唤醒间隔（10ms）
#define DEEPSLEEP_MIN_IDLE_TIME            2          // 进入深度睡眠的最小空闲时间（Tick）

/*!
    \brief      配置深度睡眠唤醒源
    \param[in]  none
    \param[out] none
    \retval     none
    \note       配置RTC周期性唤醒，唤醒间隔动态调整
*/
void config_deepsleep_wakeup(void);

/*!
    \brief      进入深度睡眠模式
    \param[in]  sleep_ticks - 预计睡眠的Tick数（以ms为单位）
    \param[out] none
    \retval     none
    \note       根据空闲时间动态调整唤醒间隔
                - 空闲时间 < 2 Tick：不进入深度睡眠
                - 空闲时间 ≤ 10ms：唤醒间隔 = 实际空闲时间
                - 空闲时间 > 10ms：唤醒间隔 = 10ms（最大限制）
*/
void enter_deepsleep_mode(uint32_t sleep_ticks);

/*!
    \brief      退出深度睡眠后的处理
    \param[in]  none
    \param[out] none
    \retval     none
    \note       唤醒后需要重新配置系统时钟
*/
void exit_deepsleep_mode(void);

/*!
    \brief      空闲任务钩子函数
    \param[in]  none
    \param[out] none
    \retval     none
    \note       当没有任何任务处于就绪状态时，调度器会执行IDLE任务
                在这个钩子中可以进入低功耗模式
                - 仅在MODE_NORMAL下进入深度睡眠
                - 获取预计空闲时间，动态决定是否进入深度睡眠
*/
void vApplicationIdleHook(void);

#endif /* __LOW_POWER_H */