#include <string.h>
#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IGMP.h"
#include "ptpd.h"
#include "ptpd_net.h"
#include "stm32f7xx_hal_ptp.h"
#include "FreeRTOS_errno_TCP.h"
#include "stm32f7xx_hal.h"

#if FREERTOS_PTPD
#define PTPD_QUEUE_SIZE (1)
static Socket_t eventSock, generalSock;
static SocketSet_t xFD_Set;
static const TickType_t xRxBlockTime = 0;
// Start all of the UDP stuff.
bool ptpd_net_init(NetPath *net_path, PtpClock *ptp_clock)
{
  /* USER CODE BEGIN 5 */
  struct freertos_sockaddr xBindAddress;
  struct freertos_ip_mreq mreq;
  BaseType_t op_result;
  /* Create the set of sockets that will be passed into FreeRTOS_select(). */
  xFD_Set = FreeRTOS_CreateSocketSet();
  /* Attempt to open the socket. */
  eventSock = FreeRTOS_socket(FREERTOS_AF_INET,
                                     FREERTOS_SOCK_DGRAM, /*FREERTOS_SOCK_DGRAM for UDP.*/
                                     FREERTOS_IPPROTO_UDP);

  /* Check the socket was created. */
  configASSERT(eventSock != FREERTOS_INVALID_SOCKET);
  // Bind
  xBindAddress.sin_addr = FreeRTOS_GetIPAddress();
  xBindAddress.sin_port = FreeRTOS_htons(PTP_EVENT_PORT);
  op_result = FreeRTOS_bind(eventSock, &xBindAddress, sizeof(xBindAddress));
  /* join multicast group*/
  mreq.imr_interface = xBindAddress;
  mreq.imr_multiaddr.sin_addr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  mreq.imr_multiaddr.sin_port = FreeRTOS_htons(PTP_EVENT_PORT);
  op_result = FreeRTOS_setsockopt(eventSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

  /* Attempt to open the socket. */
  generalSock = FreeRTOS_socket(FREERTOS_AF_INET,
                                     FREERTOS_SOCK_DGRAM, /*FREERTOS_SOCK_DGRAM for UDP.*/
                                     FREERTOS_IPPROTO_UDP);

  /* Check the socket was created. */
  configASSERT(generalSock != FREERTOS_INVALID_SOCKET);
  // Bind
  xBindAddress.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
  op_result = FreeRTOS_bind(generalSock, &xBindAddress, sizeof(xBindAddress));
  /* join multicast group*/
  mreq.imr_interface = xBindAddress;
  mreq.imr_multiaddr.sin_addr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  mreq.imr_multiaddr.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
  op_result = FreeRTOS_setsockopt(generalSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

  // set blocktimes to 0
  op_result = FreeRTOS_setsockopt( eventSock, 0, FREERTOS_SO_RCVTIMEO,
                             &xRxBlockTime, sizeof( xRxBlockTime ) );
  /* Add the created socket to the set for the READ event only. */
  FreeRTOS_FD_SET( eventSock, xFD_Set, eSELECT_READ );
  op_result = FreeRTOS_setsockopt( generalSock, 0, FREERTOS_SO_RCVTIMEO,
                             &xRxBlockTime, sizeof( xRxBlockTime ) );
  /* Add the created socket to the set for the READ event only. */
  FreeRTOS_FD_SET( generalSock, xFD_Set, eSELECT_READ );
  return true;
}

// Shut down the UDP and network stuff.
bool ptpd_net_shutdown(NetPath *net_path)
{
  //vTaskDelete(event_tsk_handle);
  //vTaskDelete(general_tsk_handle);
  //FreeRTOS_closesocket( net_path->eventSock);
  //FreeRTOS_closesocket( net_path->generalSock);
  //vQueueDelete(net_path->eventQ);
  //vQueueDelete(net_path->generalQ);
  return true;
}

// Wait for a packet  to come in on either port.  For now, there is no wait.
// Simply check to  see if a packet is available on either port and return 1,
// otherwise return 0.
int32_t ptpd_net_select(NetPath *net_path, const TimeInternal *timeout)
{
   /* Wait for any event within the socket set. */
  int op_result = 0;
  op_result = FreeRTOS_select( xFD_Set, 1000 );
        if( op_result != 0 )
        {
            /* The return value should never be zero because FreeRTOS_select() was called
            with an indefinite delay (assuming INCLUDE_vTaskSuspend is set to 1).
            Now check each socket which belongs to the set if it had an event */
          if( FreeRTOS_FD_ISSET ( eventSock, xFD_Set ) )
            {
              return 1;
          }else if( FreeRTOS_FD_ISSET ( generalSock, xFD_Set ) )
          {
            return 1;
          }

        }
    return 0;
}

// Delete all waiting packets in event queue.
void ptpd_net_empty_event_queue(NetPath *net_path)
{
  return;//if (event_queue != NULL) xQueueReset(event_queue);
  //ptp)d_net_queue_empty(&net_path->eventQ);
}


ssize_t ptpd_net_recv_event(NetPath *net_path, octet_t *buf, TimeInternal *time)
{
  int op_result = 0;
  struct freertos_sockaddr xSender;
  uint32_t xSenderSize = sizeof(xSender);
  if( FreeRTOS_FD_ISSET ( eventSock, xFD_Set ) )
  {
    /* Read from the socket. */
    op_result = FreeRTOS_recvfrom(eventSock,
             buf,
             PACKET_SIZE,
             0,
             &xSender,
             &xSenderSize);
  if(op_result >0){
    ptptime_t rxts = ethptp_get_last_rx_ts();
    time->seconds = rxts.tv_sec;
    time->nanoseconds = rxts.tv_nsec;
    return op_result;
  }
  /* Process the received data here. */
  }
  return 0;
}

ssize_t ptpd_net_recv_general(NetPath *net_path, octet_t *buf, TimeInternal *time)
{
    int op_result = 0;
  struct freertos_sockaddr xSender;
  uint32_t xSenderSize = sizeof(xSender);
  if( FreeRTOS_FD_ISSET ( generalSock, xFD_Set ) )
  {
    /* Read from the socket. */
    op_result = FreeRTOS_recvfrom(generalSock,
             buf,
             PACKET_SIZE,
             0,
             &xSender,
             &xSenderSize);
  if(op_result >0){
    ptptime_t rxts = ethptp_get_last_rx_ts();
    time->seconds = rxts.tv_sec;
    time->nanoseconds = rxts.tv_nsec;
    return op_result;
  }
  /* Process the received data here. */
  }
  return 0;
}

static ssize_t ptpd_net_send(const octet_t *buf, int16_t  length, TimeInternal *time, Socket_t sock, int32_t dest_addr, uint16_t dest_port)
{
  
  int result;
  //struct pbuf *p;
  // Allocate the tx pbuf based on the current size.
  //p = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
  //if (NULL == p)
  //{
//    syslog_printf(SYSLOG_ERROR, "PTPD: failed to allocate transmit protocol buffer");
    //ERROR("PTPD: Failed to allocate transmit protocol buffer\n");
    //goto fail01;
  //}

  // Copy the incoming data into the pbuf payload.
  //result = pbuf_take(p, buf, length);
  //if (ERR_OK != result)
  //{
  //  syslog_printf(SYSLOG_ERROR, "PTPD: failed to copy data into protocol buffer (%d)", result);
  //  ERROR("PTPD: Failed to copy data into protocol buffer (%d)\n", result);
  //  length = 0;
  //  goto fail02;
  //}

  // Send the buffer.
  //result = udp_sendto(pcb, p, (void *)addr, pcb->local_port);
  struct freertos_sockaddr dest;
  dest.sin_addr = dest_addr;
  dest.sin_port = FreeRTOS_htons(dest_port);
  result = FreeRTOS_sendto(sock, buf, length, 0, &dest, sizeof(dest));
  //if (ERR_OK != result)
  //{
//    syslog_printf(SYSLOG_ERROR, "PTPD: failed to send data (%d)", result);
    ERROR("PTPD: Failed to send data (%d)\n", result);
    //length = 0;
    //goto fail02;
  //}

#if defined(STM32F4) || defined(STM32F7)
  // Fill in the timestamp of the buffer just sent.
  if (time != NULL)
  {
    // We have special call back into the Ethernet interface to fill the timestamp
    // of the buffer just transmitted. This call will block for up to a certain amount
    // of time before it may fail if a timestamp was not obtained.
    //ethernetif_get_tx_timestamp(p);
    // use ethptp_get_last_tx_ts() instead
    ptptime_t txts = ethptp_get_last_tx_ts();
    time->seconds = txts.tv_sec;
    time->nanoseconds = txts.tv_nsec;
  }
#endif

  // Get the timestamp of the sent buffer.  We avoid overwriting 
  // the time if it looks to be an invalid zero value.
//  if ((time != NULL) && (p->time_sec != 0))
//  {
//    time->seconds = p->time_sec;
//    time->nanoseconds = p->time_nsec;
//    DBGV("PTPD: %d sec %d nsec\n", time->seconds, time->nanoseconds);
//  }

  return length;
}

ssize_t ptpd_net_send_event(NetPath *net_path, const octet_t *buf, int16_t  length, TimeInternal *time)
{
  uint32_t maddr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  return ptpd_net_send(buf, length, time, eventSock, maddr, PTP_EVENT_PORT );
}

ssize_t ptpd_net_send_peer_event(NetPath *net_path, const octet_t *buf, int16_t  length, TimeInternal* time)
{
  //return ptpd_net_send(buf, length, time, &net_path->peerMulticastAddr, net_path->eventPcb);
  return 0;
}

ssize_t ptpd_net_send_general(NetPath *net_path, const octet_t *buf, int16_t  length)
{
  uint32_t maddr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  return ptpd_net_send(buf, length, NULL, generalSock, maddr, PTP_GENERAL_PORT );
  //return ptpd_net_send(buf, length, NULL, &net_path->multicastAddr, net_path->generalPcb);
  //return 0;//return ptpd_net_send(buf, length, NULL, net_path->generalSock, net_path->multicastAddr, PTP_GENERAL_PORT );
}

ssize_t ptpd_net_send_peer_general(NetPath *net_path, const octet_t *buf, int16_t  length)
{
  //return ptpd_net_send(buf, length, NULL, &net_path->peerMulticastAddr, net_path->generalPcb);
  return 0;
}

#endif
