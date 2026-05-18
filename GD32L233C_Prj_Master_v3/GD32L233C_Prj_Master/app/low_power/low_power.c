#include "low_power.h"
#include "tasks.h"
#include <stdio.h>

/*!
    \brief      配置深度睡眠唤醒源
    \param[in]  none
    \param[out] none
    \retval     none
    \note       配置RTC周期性唤醒，每10ms唤醒一次
*/
void config_deepsleep_wakeup(void)
{
    /* 使能PMU时钟 */
    rcu_periph_clock_enable(RCU_PMU);

    /* 使能RTC时钟 - RTC用作周期性唤醒源 */
    rcu_periph_clock_enable(RCU_RTC);

    /* 使能备份域时钟 */
    rcu_periph_clock_enable(RCU_BKPI);

    /* 配置RTC时钟源（通常使用LXTAL或LRC） */
    /* 等待RTC时钟稳定 */
    rcu_osci_on(RCU_LXTAL);
    rcu_osci_stab_wait(RCU_LXTAL);
    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);

    /* 配置RTC唤醒闹钟 - 每10ms唤醒一次 */
    /* RTC时钟通常为32.768kHz */
    /* 唤醒周期 = 32768 / 100 = 328 个时钟周期 ≈ 10ms */
    rtc_prescaler_set(32768 / 100);

    /* 配置RTC闹钟为周期性模式 */
    rtc_interrupt_enable(RTC_INT_ALARM);

    /* 清除唤醒标志 */
    pmu_flag_clear(PMU_FLAG_WAKEUP);

    /* 使能唤醒引脚功能 */
    pmu_wakeup_pin_enable();

    printf("[LOW_POWER] Deep sleep wakeup source configured (RTC)\r\n");
}

/*!
    \brief      进入深度睡眠模式
    \param[in]  sleep_ticks - 预计睡眠的Tick数（以ms为单位）
    \param[out] none
    \retval     none
    \note       根据空闲时间动态调整唤醒间隔
*/
void enter_deepsleep_mode(uint32_t sleep_ticks)
{
    uint32_t wakeup_interval;

    /* 如果睡眠时间很短，不进入深度睡眠 */
    if(sleep_ticks < DEEPSLEEP_MIN_IDLE_TIME) {
        return;
    }

    /* 限制单次睡眠时间，避免睡太久 */
    if(sleep_ticks > DEEPSLEEP_WAKEUP_INTERVAL_MS) {
        wakeup_interval = DEEPSLEEP_WAKEUP_INTERVAL_MS;
    } else {
        /* 动态调整唤醒间隔 = 实际空闲时间（避免过度睡眠） */
        wakeup_interval = sleep_ticks;
    }

    /* 配置RTC闹钟唤醒周期 */
    rtc_alarm_set(wakeup_interval);

    /* 清除所有待处理的中断标志 */
    __HAL_RTC_CLEAR_FLAG(RTC_FLAG_ALRAF);
    __HAL_EXTI_CLEAR_FLAG();

    /* 进入深度睡眠模式 */
    pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_DEEPSLEEP_ENTRY);
}

/*!
    \brief      退出深度睡眠后的处理
    \param[in]  none
    \param[out] none
    \retval     none
    \note       唤醒后需要重新配置系统时钟
*/
void exit_deepsleep_mode(void)
{
    /* 重新配置系统时钟 */
    SystemCoreClockUpdate();

    /* 清除唤醒标志 */
    pmu_flag_clear(PMU_FLAG_WAKEUP);
}

/*!
    \brief      空闲任务钩子函数
    \param[in]  none
    \param[out] none
    \retval     none
    \note       当没有任何任务处于就绪状态时，调度器会执行IDLE任务
                在这个钩子中可以进入低功耗模式
*/
void vApplicationIdleHook(void)
{
    /* 只有在正常模式下才能进入深度睡眠 */
    /* 在WARNING和DANGER模式下需要保持活跃 */
    if(get_system_mode() == MODE_NORMAL) {
        /* 获取FreeRTOS预计的空闲时间（单位：Tick） */
        /* vTaskGetExpectedIdleTime() 返回下一个任务需要唤醒前的空闲时间 */
        uint32_t expected_idle_time = uxTaskGetExpectedIdleTime();

        /* 检查是否有足够的空闲时间进入深度睡眠（至少2个Tick） */
        if(expected_idle_time >= DEEPSLEEP_MIN_IDLE_TIME) {
            /* 进入深度睡眠，唤醒间隔会动态调整为实际空闲时间 */
            enter_deepsleep_mode(expected_idle_time);
        }
    }
}

/*!
    \brief      RTC闹钟中断处理
    \param[in]  none
    \param[out] none
    \retval     none
    \note       RTC闹钟中断用于从深度睡眠中唤醒
*/
void RTC_AlarmHandler(void)
{
    if(rtc_flag_get(RTC_FLAG_ALARM) != RESET) {
        /* 清除闹钟标志 */
        rtc_flag_clear(RTC_FLAG_ALARM);

        /* 退出深度睡眠后的处理 */
        exit_deepsleep_mode();
    }
}