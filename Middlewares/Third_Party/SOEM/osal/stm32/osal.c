#include "../osal.h"
#include "osal_defs.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx.h"  /* CoreDebug, DWT registers */
#include <stdlib.h>

/* ---- DWT Cycle Counter for µs-resolution timing ---- */
static uint32 dwt_cpu_freq = 0;  /* CPU frequency in Hz (set by osal_dwt_init) */

void osal_dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* Enable trace */
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;  /* Enable cycle counter */
  dwt_cpu_freq = HAL_RCC_GetSysClockFreq();  /* e.g. 600000000 for 600 MHz */
  if (dwt_cpu_freq == 0U)
  {
    dwt_cpu_freq = 600000000U;  /* fallback */
  }
}

/* Return current time with µs resolution using DWT CYCCNT */
static uint32 dwt_get_us(void)
{
  /* CYCCNT wraps every ~7.16 seconds at 600 MHz.
   * For short timeout measurements (< 7s) this is fine. */
  return (uint32)(DWT->CYCCNT / (dwt_cpu_freq / 1000000U));
}

static void osal_set_time_from_tick(uint32 tick_ms, ec_timet *ts)
{
  if (ts == NULL)
  {
    return;
  }
  /* Combine ms-level HAL tick with µs-level DWT for sub-ms precision */
  ts->tv_sec = (int64)(tick_ms / 1000U);
  /* Use DWT for sub-ms precision within the current ms */
  uint32 us_in_ms = dwt_get_us() % 1000U;
  ts->tv_nsec = (int32)((tick_ms % 1000U) * 1000000U + us_in_ms * 1000U);
}

void osal_get_monotonic_time(ec_timet *ts)
{
  osal_set_time_from_tick(HAL_GetTick(), ts);
}

ec_timet osal_current_time(void)
{
  ec_timet ts;
  osal_set_time_from_tick(HAL_GetTick(), &ts);
  return ts;
}

void osal_time_diff(ec_timet *start, ec_timet *end, ec_timet *diff)
{
  if ((start == NULL) || (end == NULL) || (diff == NULL))
  {
    return;
  }
  diff->tv_sec = end->tv_sec - start->tv_sec;
  diff->tv_nsec = end->tv_nsec - start->tv_nsec;
  if (diff->tv_nsec < 0)
  {
    diff->tv_sec -= 1;
    diff->tv_nsec += 1000000000L;
  }
}

void osal_timer_start(osal_timert *self, uint32 timeout_usec)
{
  ec_timet now = osal_current_time();
  uint32 sec = timeout_usec / 1000000U;
  uint32 usec = timeout_usec % 1000000U;
  self->stop_time.tv_sec = now.tv_sec + (int64)sec;
  self->stop_time.tv_nsec = now.tv_nsec + (int32)(usec * 1000U);
  if (self->stop_time.tv_nsec >= 1000000000L)
  {
    self->stop_time.tv_sec += 1;
    self->stop_time.tv_nsec -= 1000000000L;
  }
}

boolean osal_timer_is_expired(osal_timert *self)
{
  ec_timet now = osal_current_time();
  if (now.tv_sec > self->stop_time.tv_sec)
  {
    return TRUE;
  }
  if (now.tv_sec == self->stop_time.tv_sec)
  {
    return (now.tv_nsec >= self->stop_time.tv_nsec) ? TRUE : FALSE;
  }
  return FALSE;
}

int osal_usleep(uint32 usec)
{
  if (dwt_cpu_freq == 0U)
  {
    /* DWT not initialised yet — fallback */
    if (usec >= 1000U)
    {
      HAL_Delay(usec / 1000U);
    }
    return 0;
  }
  uint32 cycles_per_us = dwt_cpu_freq / 1000000U;
  uint32 start = DWT->CYCCNT;
  uint32 target = usec * cycles_per_us;
  while ((DWT->CYCCNT - start) < target)
  {
    /* busy-wait with cycle-accurate precision */
  }
  return 0;
}

int osal_monotonic_sleep(ec_timet *ts)
{
  ec_timet now = osal_current_time();
  ec_timet diff;
  osal_time_diff(&now, ts, &diff);
  if (diff.tv_sec < 0)
  {
    return 0;
  }
  uint64 usec = (uint64)diff.tv_sec * 1000000ULL + (uint64)(diff.tv_nsec / 1000L);
  return osal_usleep((uint32)usec);
}

void *osal_malloc(size_t size)
{
  return malloc(size);
}

void osal_free(void *ptr)
{
  free(ptr);
}

int osal_thread_create(void *thandle, int stacksize, void *func, void *param)
{
  (void)thandle;
  (void)stacksize;
  (void)func;
  (void)param;
  return 0;
}

int osal_thread_create_rt(void *thandle, int stacksize, void *func, void *param)
{
  return osal_thread_create(thandle, stacksize, func, param);
}

void *osal_mutex_create(void)
{
  osal_mutex_t *mutex = (osal_mutex_t *)malloc(sizeof(osal_mutex_t));
  if (mutex != NULL)
  {
    mutex->lock = 0U;
  }
  return mutex;
}

void osal_mutex_destroy(void *mutex)
{
  if (mutex != NULL)
  {
    free(mutex);
  }
}

void osal_mutex_lock(void *mutex)
{
  osal_mutex_t *m = (osal_mutex_t *)mutex;
  if (m == NULL)
  {
    return;
  }
  for (;;)
  {
    uint32 primask = __get_PRIMASK();
    __disable_irq();
    if (m->lock == 0U)
    {
      m->lock = 1U;
      __set_PRIMASK(primask);
      return;
    }
    __set_PRIMASK(primask);
  }
}

void osal_mutex_unlock(void *mutex)
{
  osal_mutex_t *m = (osal_mutex_t *)mutex;
  if (m == NULL)
  {
    return;
  }
  uint32 primask = __get_PRIMASK();
  __disable_irq();
  m->lock = 0U;
  __set_PRIMASK(primask);
}
