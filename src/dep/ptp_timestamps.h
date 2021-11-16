#ifndef PTP_TIMESTAMPS_H
# define PTP_TIMESTAMPS_H
#include "datatypes.h"

/**
 * @brief setup the timestamping hardware
 * 
 * @param rtOpts the runtime config (may not be needed?)
 * @return uint8_t return 1 if successfull 0 if error
 */
uint8_t init_timestamping(RunTimeOpts *rtOpts);
/**
 * @brief retrieve the hardware timestamp for an event message.
 * 
 * @param ts_out pointer to structure in which to store the ts.
 * @return uint8_t return 1 if successfull 0 if no ts was retrieved
 */
uint8_t retrieve_timestamp(TimeInternal *ts_out);

#endif