#include "ptpd.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#if LWIP_PTPD

typedef uint16_t TimerID_t;
// Static array of PTPD timers.
//static osTimerId_t ptpd_timer_id[TIMER_ARRAY_SIZE];
static TimerID_t ptpd_timer_id[TIMER_ARRAY_SIZE];
static TimerHandle_t ptpd_timer_handle[TIMER_ARRAY_SIZE];
static bool ptpd_timers_expired[TIMER_ARRAY_SIZE];


// Callback for timers.
//static void ptpd_timer_callback(void *arg)TimerHandle_t xTimer
static void ptpd_timer_callback(TimerHandle_t xTimer)
{
  TimerID_t index = *((TimerID_t *)pvTimerGetTimerID(xTimer));
  if (index >= TIMER_ARRAY_SIZE){
    // no valid timer id
    return;
  }
  // Sanity check the index.
  if (index < TIMER_ARRAY_SIZE)
  {
    // Mark the indicated timer as expired.
    ptpd_timers_expired[index] = true;

    // Notify the PTP thread of a pending operation.
    ptpd_alert();
  }
}

// Initialize PTPD timers.
void ptpd_timer_init(void)
{
  int32_t i;

  // Static timer control block array.
  //static uint32_t ptpd_timer_cb[TIMER_ARRAY_SIZE][sizeof(osRtxTimerFimfi_t)/4U] __attribute__((section(".bss.os.timer.cb")));

  // Create the various timers used in the system.
  for (i = 0; i < TIMER_ARRAY_SIZE; i++)
  {
    // Initialize the timer attributes.
    /*osTimerAttr_t timer_attrs =
    {
      .name = "ptpd",
      .attr_bits = 0U,
      .cb_mem = ptpd_timer_cb[i],
      .cb_size = sizeof(ptpd_timer_cb[i])
    };*/

    // Mark the timer as not expired.
    ptpd_timers_expired[i] = true;
    // Create the timer.
    //ptpd_timer_id[i] = osTimerNew(ptpd_timer_callback, osTimerPeriodic, (void *) i, &timer_attrs);
    ptpd_timer_handle[i] = NULL;//xTimerCreate("ptp", )
    ptpd_timer_id[i] = i;
  }
  
}

// Start the indexed timer with the given interval.
void ptpd_timer_start(int32_t index, uint32_t interval_ms)
{
  // Sanity check the index.
  if (index >= TIMER_ARRAY_SIZE) return;
  //if (ptpd_timer_id[index] == 0) return;

  //DBGV("PTPD: set timer %d to %u\n", index, interval_ms);

  // Reset the timer expired flag.
  ptpd_timers_expired[index] = false;
  // if timer is not yet created, create timer
  if (ptpd_timer_handle[index] == 0)
  {
    ptpd_timer_handle[index] = xTimerCreate("ptp", pdMS_TO_TICKS(interval_ms), pdTRUE, &(ptpd_timer_id[index]), ptpd_timer_callback);
    
  }
  // change timer period and state, block indefinetly until successfull.
  xTimerChangePeriod(ptpd_timer_handle[index], pdMS_TO_TICKS(interval_ms), portMAX_DELAY);
  xTimerStart(ptpd_timer_handle[index], portMAX_DELAY);
  // Start the timer with the specified duration.
  //osTimerStart(ptpd_timer_id[index], tick_from_milliseconds(interval_ms));
}

// Stop the indexed timer.
void ptpd_timer_stop(int32_t index)
{
  // Sanity check the index.
  if (index >= TIMER_ARRAY_SIZE) return;
  if(ptpd_timer_handle[index] == NULL) return;

  DBGV("PTPD: stop timer %d\n", index);

  // Stop the timer.
  //osTimerStop(ptpd_timer_id[index]);
  xTimerStop(ptpd_timer_handle[index], portMAX_DELAY);
  // Reset the expired flag.
  ptpd_timers_expired[index] = false;
}

// If the timer has expired, this function will reset the
// expired flag and return true, otherwise it will false.
bool ptpd_timer_expired(int32_t index)
{
  // Sanity check the index.
  if (index >= TIMER_ARRAY_SIZE) return false;

  DBGV("PTPD: timer %d %s\n", index,
        ptpd_timers_expired[index] ? "expired" : "not expired");

  // Return false if the timer hasn't expired.
  if (!ptpd_timers_expired[index]) return false;

  // We only return the timer expired once.
  ptpd_timers_expired[index] = false;

  // Return true since the timer expired.
  return true;
}

#endif // LWIP_PTPD
