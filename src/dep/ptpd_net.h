#ifndef PTPD_NET_H
#define PTPD_NET_H
#include "stm32f7xx_hal_ptp.h"
typedef struct packet_buffer{
    ptptime_t timestamp;
    uint32_t length;
    uint8_t *buffer;

}packet_buffer_t;
#endif