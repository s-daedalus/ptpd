#include <string.h>
#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IGMP.h"
#include "ptpd.h"
#include "stm32f7xx_hal_ptp.h"
#include "FreeRTOS_errno_TCP.h"
#include "stm32f7xx_hal.h"
#include "stm32f7xx_hal_ptp.h"


#if FREERTOS_PTPD
// the Networking interface Sockets
static Socket_t eventSock = NULL, generalSock = NULL;
static SocketSet_t xFD_Set = NULL;
// constant 0 for socket block times
static const TickType_t zeroBlockTime = 0;


// Start all of the UDP stuff.
bool ptpd_net_init(PtpClock *ptp_clock)
{
  // Declare and setup addresses for event socket
  struct freertos_sockaddr bind_addr = {
    .sin_addr = FreeRTOS_GetIPAddress(),
    .sin_port = FreeRTOS_htons(PTP_EVENT_PORT)
  };
  struct freertos_ip_mreq mreq = {
    .imr_interface = bind_addr,
    .imr_multiaddr = {
      .sin_addr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS),
      .sin_port = FreeRTOS_htons(PTP_EVENT_PORT)
    }
  };
  BaseType_t op_result = 0;
  // Create the set of sockets that will be passed into FreeRTOS_select().
  if(xFD_Set != NULL){
    FreeRTOS_DeleteSocketSet(xFD_Set);
  }
  xFD_Set = FreeRTOS_CreateSocketSet();
  // Attempt to open the socket.
  if(eventSock != NULL)
  {
    FreeRTOS_closesocket(eventSock);
  }
  eventSock = FreeRTOS_socket(FREERTOS_AF_INET,
                              FREERTOS_SOCK_DGRAM,
                              FREERTOS_IPPROTO_UDP);

  //Check the socket was created.
  configASSERT(eventSock != FREERTOS_INVALID_SOCKET);
  // Bind
  op_result = FreeRTOS_bind(
                            eventSock,
                            &bind_addr,
                            sizeof(bind_addr)
                          );
  /* join multicast group*/
  op_result = FreeRTOS_setsockopt(
                                  eventSock,
                                  FREERTOS_IPPROTO_UDP,
                                  FREERTOS_SO_IP_ADD_MEMBERSHIP,
                                  &mreq,
                                  sizeof(mreq)
                                );
  
  // General Socket creation ad setup
  // create socket 
  if (generalSock != NULL)
  {
    FreeRTOS_closesocket(generalSock);
  }
  generalSock = FreeRTOS_socket(FREERTOS_AF_INET,
                                FREERTOS_SOCK_DGRAM,
                                FREERTOS_IPPROTO_UDP);

  // check if the socket is created
  configASSERT(generalSock != FREERTOS_INVALID_SOCKET);
  // change port of bind_addr to general port
  bind_addr.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
  op_result = FreeRTOS_bind(generalSock, &bind_addr, sizeof(bind_addr));
  /* join multicast group*/
  mreq.imr_multiaddr.sin_addr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  mreq.imr_multiaddr.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
  op_result = FreeRTOS_setsockopt(
                                  generalSock,
                                  FREERTOS_IPPROTO_UDP,
                                  FREERTOS_SO_IP_ADD_MEMBERSHIP, 
                                  &mreq, sizeof(mreq)
                                );
  // set blocktimes to 0
  op_result = FreeRTOS_setsockopt(eventSock, 0, FREERTOS_SO_RCVTIMEO,
                                  &zeroBlockTime, sizeof(zeroBlockTime)
                                );
  op_result = FreeRTOS_setsockopt(generalSock, 0, FREERTOS_SO_RCVTIMEO,
                                  &zeroBlockTime, sizeof(zeroBlockTime)
                                );
  
  // Add the created socket to the set for the read event only.
  FreeRTOS_FD_SET(eventSock, xFD_Set, eSELECT_READ);
  FreeRTOS_FD_SET(generalSock, xFD_Set, eSELECT_READ);
  /// TODO: if op_result is bad return false (after each op)
  return true;
}

// Shut down the UDP and network stuff.
bool ptpd_net_shutdown()
{
  FreeRTOS_DeleteSocketSet(xFD_Set);
  FreeRTOS_closesocket(eventSock);
  FreeRTOS_closesocket(generalSock);
  xFD_Set = NULL;
  eventSock = NULL;
  generalSock = 0;
  return true;
}

// Wait for a packet  to come in on either port.  For now, there is no wait.
// Simply check to  see if a packet is available on either port and return 1,
// otherwise return 0.
int32_t ptpd_net_select(const TimeInternal *timeout)
{
  // if network is not initialized return 0
  if(xFD_Set == NULL || eventSock == NULL || generalSock == NULL)
  {
    return 0;
  }
  /* Wait for any event within the socket set. */
  int op_result = 0;
  op_result = FreeRTOS_select(xFD_Set, PTPD_SELECT_BLOCK_TIME);
  if (op_result != 0)
  {
    if (FreeRTOS_FD_ISSET(eventSock, xFD_Set))
    {
      return 1;
    }
    else if (FreeRTOS_FD_ISSET(generalSock, xFD_Set))
    {
      return 1;
    }
  }
  return 0;
}

ssize_t ptpd_net_recv_event(octet_t *buf, TimeInternal *time)
{
  if(eventSock == NULL || xFD_Set == NULL){
    return 0;
  }
  int op_result = 0;
  struct freertos_sockaddr xSender;
  uint32_t xSenderSize = sizeof(xSender);
  if (FreeRTOS_FD_ISSET(eventSock, xFD_Set))
  {
    op_result = FreeRTOS_recvfrom(eventSock,
                                  buf,
                                  PACKET_SIZE,
                                  0,
                                  &xSender,
                                  &xSenderSize);
    if (op_result > 0)
    {
      ptptime_t rxts = ethptp_get_last_rx_ts();
      time->seconds = rxts.tv_sec;
      time->nanoseconds = rxts.tv_nsec;
      return op_result;
    }
  }
  return 0;
}

ssize_t ptpd_net_recv_general(octet_t *buf, TimeInternal *time)
{
  if(generalSock == NULL || xFD_Set == NULL){
    return 0;
  }
  int op_result = 0;
  struct freertos_sockaddr xSender;
  uint32_t xSenderSize = sizeof(xSender);
  if (FreeRTOS_FD_ISSET(generalSock, xFD_Set))
  {
    
    op_result = FreeRTOS_recvfrom(generalSock,
                                  buf,
                                  PACKET_SIZE,
                                  0,
                                  &xSender,
                                  &xSenderSize);
    if (op_result > 0)
    {
      ptptime_t rxts = ethptp_get_last_rx_ts();
      time->seconds = rxts.tv_sec;
      time->nanoseconds = rxts.tv_nsec;
      return op_result;
    }
  }
  return 0;
}

static ssize_t ptpd_net_send(const octet_t *buf, int16_t length, TimeInternal *time, Socket_t sock, int32_t dest_addr, uint16_t dest_port)
{

  int result;
  struct freertos_sockaddr dest;
  dest.sin_addr = dest_addr;
  dest.sin_port = FreeRTOS_htons(dest_port);
  result = FreeRTOS_sendto(sock, buf, length, 0, &dest, sizeof(dest));

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

  return length;
}

ssize_t ptpd_net_send_event(const octet_t *buf, int16_t length, TimeInternal *time)
{
  uint32_t maddr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  return ptpd_net_send(buf, length, time, eventSock, maddr, PTP_EVENT_PORT);
}

ssize_t ptpd_net_send_peer_event(const octet_t *buf, int16_t length, TimeInternal *time)
{
  //return ptpd_net_send(buf, length, time, &net_path->peerMulticastAddr, net_path->eventPcb);
  return 0;
}

ssize_t ptpd_net_send_general(const octet_t *buf, int16_t length)
{
  uint32_t maddr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  return ptpd_net_send(buf, length, NULL, generalSock, maddr, PTP_GENERAL_PORT);
  //return ptpd_net_send(buf, length, NULL, &net_path->multicastAddr, net_path->generalPcb);
  //return 0;//return ptpd_net_send(buf, length, NULL, net_path->generalSock, net_path->multicastAddr, PTP_GENERAL_PORT );
}

ssize_t ptpd_net_send_peer_general(const octet_t *buf, int16_t length)
{
  //return ptpd_net_send(buf, length, NULL, &net_path->peerMulticastAddr, net_path->generalPcb);
  return 0;
}

#endif
