#include <string.h>
#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IGMP.h"
#include "ptpd.h"
#include "stm32f7xx_hal_ptp.h"


#if FREERTOS_PTPD
#define PTPD_QUEUE_SIZE (1)
static uint8_t event_pbuf[PACKET_SIZE];
static uint8_t general_pbuf[PACKET_SIZE];
static TaskHandle_t event_tsk_handle;
static TaskHandle_t general_tsk_handle;
static QueueSetHandle_t queueSet;
static void ptpd_net_event_task(void *args)
{

  NetPath * net_path = (NetPath*) args;
  struct freertos_sockaddr sender;
  for(;;){
    // block on recvfrom call
    int result = FreeRTOS_recvfrom(net_path->eventSock, event_pbuf, PACKET_SIZE, 0, &sender, sizeof(struct freertos_sockaddr));
    if (result){
      // receive from event socket, enqueue received packet
      result = xQueueSendToBack(net_path->eventQ, event_pbuf, portMAX_DELAY);
    }
  }
}

static void ptpd_net_general_task(void *args)
{
  NetPath * net_path = (NetPath*) args;
  struct freertos_sockaddr sender;
  for(;;){
    // block on recvfrom call
    int result = FreeRTOS_recvfrom(net_path->generalSock, event_pbuf, PACKET_SIZE, 0, &sender, sizeof(struct freertos_sockaddr));
    if (result){
      // receive from event socket, enqueue received packet
      result = xQueueSendToBack(net_path->generalQ, event_pbuf, portMAX_DELAY);
    }
  }
}

// Start all of the UDP stuff.
bool ptpd_net_init(NetPath *net_path, PtpClock *ptp_clock)
{
  net_path->eventQ = xQueueCreate(PTPD_QUEUE_SIZE, PACKET_SIZE);
  net_path->generalQ = xQueueCreate(PTPD_QUEUE_SIZE, PACKET_SIZE);
  queueSet = xQueueCreateSet(PTPD_QUEUE_SIZE * 2);  
  xQueueAddToSet(net_path->eventQ, queueSet);
  xQueueAddToSet(net_path->generalQ, queueSet);
  // init NetPath with addresses
  // Open lwip raw udp interfaces for the event port.
  // Open lwip raw udp interfaces for the general port.
  // Configure network (broadcast/unicast) addresses (unicast disabled).
  net_path->unicastAddr = 0;
  // Init general multicast IP address.
  net_path->multicastAddr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  struct freertos_sockaddr address;

  address.sin_addr = FreeRTOS_GetIPAddress();
  address.sin_port = FreeRTOS_htons(PTP_EVENT_PORT);

  net_path->eventSock = FreeRTOS_socket(
    FREERTOS_AF_INET,
    FREERTOS_SOCK_DGRAM,
    FREERTOS_IPPROTO_UDP
  );
  // bind
  FreeRTOS_bind(net_path->eventSock, &address, sizeof(struct freertos_sockaddr));
  // join igmp
  struct freertos_ip_mreq mreq;
  mreq.imr_interface = address;
  mreq.imr_multiaddr.sin_addr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  mreq.imr_multiaddr.sin_port = FreeRTOS_htons(PTP_EVENT_PORT);
  FreeRTOS_setsockopt(net_path->eventSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
  // prepare event sock:
  // they share the same interface address (our address)
  address.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
  net_path->generalSock = FreeRTOS_socket(
    FREERTOS_AF_INET,
    FREERTOS_SOCK_DGRAM,
    FREERTOS_IPPROTO_UDP
  );
  // bind
  FreeRTOS_bind(net_path->generalSock, &address, sizeof(struct freertos_sockaddr));
  // join igmp
  struct freertos_ip_mreq mreq;
  mreq.imr_interface = address;
  mreq.imr_multiaddr.sin_addr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
  mreq.imr_multiaddr.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
  FreeRTOS_setsockopt(net_path->generalSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
  
  // ######## for now no p2p delay measurement ################
  // Join multicast group (for receiving) on specified interface.
  // igmp_joingroup(&interface_addr, (ip4_addr_t *) &net_addr);
  //net_path->peerMulticastAddr = FreeRTOS_inet_addr(PEER_PTP_DOMAIN_ADDRESS);


  // Join peer multicast group (for receiving) on specified interface.
  // igmp_joingroup(&interface_addr, (ip4_addr_t *) &net_addr);

  // Multicast send only on specified interface.
  
  // Establish the appropriate UDP bindings/connections for event port.
  //udp_recv(net_path->eventPcb, ptpd_net_event_callback, net_path);
  //udp_bind(net_path->eventPcb, IP_ADDR_ANY, PTP_EVENT_PORT);
  // udp_connect(net_path->eventPcb, &net_addr, PTP_EVENT_PORT);

  // Establish the appropriate UDP bindings/connections for general port.
  //udp_recv(net_path->generalPcb, ptpd_net_general_callback, net_path);
  //udp_bind(net_path->generalPcb, IP_ADDR_ANY, PTP_GENERAL_PORT);
  // udp_connect(net_path->generalPcb, &net_addr, PTP_GENERAL_PORT);

  // Return success.
  return true;
}

// Shut down the UDP and network stuff.
bool ptpd_net_shutdown(NetPath *net_path)
{
  vTaskDelete(event_tsk_handle);
  vTaskDelete(general_tsk_handle);
  FreeRTOS_closesocket( net_path->eventSock);
  FreeRTOS_closesocket( net_path->generalSock);
  vQueueDelete(net_path->eventQ);
  vQueueDelete(net_path->generalQ);
  return true;
}

// Wait for a packet  to come in on either port.  For now, there is no wait.
// Simply check to  see if a packet is available on either port and return 1,
// otherwise return 0.
int32_t ptpd_net_select(NetPath *net_path, const TimeInternal *timeout)
{
  QueueSetMemberHandle_t activated = xQueueSelectFromSet( queueSet, portMAX_DELAY);
  // block until we receive
  return activated;
  // Check the packet queues.  If there is data, return true.
  //if (ptpd_net_queue_check(&net_path->eventQ) || ptpd_net_queue_check(&net_path->generalQ)) return 1;

  //return 0;
}

// Delete all waiting packets in event queue.
void ptpd_net_empty_event_queue(NetPath *net_path)
{
  ptpd_net_queue_empty(&net_path->eventQ);
}

// Receive the next buffer from the given queue.
static ssize_t ptpd_net_recv(octet_t *buf, TimeInternal *time, BufQueue *queue)
{
  int i;
  int j;
  u16_t length;
  struct pbuf *p;
  struct pbuf *pcopy;

  // Get the next buffer from the queue.
  if ((p = (struct pbuf*) ptpd_net_queue_get(queue)) == NULL)
  {
    return 0;
  }

  // Verify that we have enough space to store the contents.
  if (p->tot_len > PACKET_SIZE)
  {
    syslog_printf(SYSLOG_ERROR, "PTPD: received truncated packet");
    ERROR("PTPD: received truncated message\n");
    pbuf_free(p);
    return 0;
  }

  // Verify there is contents to copy.
  if (p->tot_len == 0)
  {
    syslog_printf(SYSLOG_ERROR, "PTPD: received empty packet");
    ERROR("PTPD: received empty packet\n");
    pbuf_free(p);
    return 0;
  }

  // Get the timestamp of the packet.
  if (time != NULL)
  {
    ptptime_t ts_time = ethptp_get_last_rx_ts();
    time->seconds = ts_time.tv_sec;
    time->nanoseconds = ts_time.tv_nsec;
  }

  // Get the length of the buffer to copy.
  length = p->tot_len;

  // Copy the pbuf payload into the buffer.
  pcopy = p;
  j = 0;
  for (i = 0; i < length; i++)
  {
    // Copy the next byte in the payload.
    buf[i] = ((u8_t *)pcopy->payload)[j++];

    // Skip to the next buffer in the payload?
    if (j == pcopy->len)
    {
      // Move to the next buffer.
      pcopy = pcopy->next;
      j = 0;
    }
  }

  // Free up the pbuf (chain).
  pbuf_free(p);

  return length;
}

ssize_t ptpd_net_recv_event(NetPath *net_path, octet_t *buf, TimeInternal *time)
{
  //return ptpd_net_recv(buf, time, &net_path->eventQ);
  // get element from event rx queue
  xQueueReceive(net_path->eventQ, buf, portMAX_DELAY);
  // get rx_timestamp
  if (time !=NULL){
    ptptime_t ts_time = ethptp_get_last_rx_ts();
    time->seconds = ts_time.tv_sec;
    time->nanoseconds = ts_time.tv_nsec;
  }

}

ssize_t ptpd_net_recv_general(NetPath *net_path, octet_t *buf, TimeInternal *time)
{
  return ptpd_net_recv(buf, time, &net_path->generalQ);
}

static ssize_t ptpd_net_send(const octet_t *buf, int16_t  length, TimeInternal *time, const int32_t * addr, struct udp_pcb * pcb)
{
  err_t result;
  struct pbuf *p;

  // Allocate the tx pbuf based on the current size.
  p = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
  if (NULL == p)
  {
    syslog_printf(SYSLOG_ERROR, "PTPD: failed to allocate transmit protocol buffer");
    ERROR("PTPD: Failed to allocate transmit protocol buffer\n");
    goto fail01;
  }

  // Copy the incoming data into the pbuf payload.
  result = pbuf_take(p, buf, length);
  if (ERR_OK != result)
  {
    syslog_printf(SYSLOG_ERROR, "PTPD: failed to copy data into protocol buffer (%d)", result);
    ERROR("PTPD: Failed to copy data into protocol buffer (%d)\n", result);
    length = 0;
    goto fail02;
  }

  // Send the buffer.
  result = udp_sendto(pcb, p, (void *)addr, pcb->local_port);
  if (ERR_OK != result)
  {
    syslog_printf(SYSLOG_ERROR, "PTPD: failed to send data (%d)", result);
    ERROR("PTPD: Failed to send data (%d)\n", result);
    length = 0;
    goto fail02;
  }

#if defined(STM32F4) || defined(STM32F7)
  // Fill in the timestamp of the buffer just sent.
  if (time != NULL)
  {
    // We have special call back into the Ethernet interface to fill the timestamp
    // of the buffer just transmitted. This call will block for up to a certain amount
    // of time before it may fail if a timestamp was not obtained.
    ethernetif_get_tx_timestamp(p);
    // use ethptp_get_last_tx_ts() instead
  }
#endif

  // Get the timestamp of the sent buffer.  We avoid overwriting 
  // the time if it looks to be an invalid zero value.
  if ((time != NULL) && (p->time_sec != 0))
  {
    time->seconds = p->time_sec;
    time->nanoseconds = p->time_nsec;
    DBGV("PTPD: %d sec %d nsec\n", time->seconds, time->nanoseconds);
  }

fail02:
  pbuf_free(p);

fail01:
  return length;
}

ssize_t ptpd_net_send_event(NetPath *net_path, const octet_t *buf, int16_t  length, TimeInternal *time)
{
  return ptpd_net_send(buf, length, time, &net_path->multicastAddr, net_path->eventPcb);
}

ssize_t ptpd_net_send_peer_event(NetPath *net_path, const octet_t *buf, int16_t  length, TimeInternal* time)
{
  return ptpd_net_send(buf, length, time, &net_path->peerMulticastAddr, net_path->eventPcb);
}

ssize_t ptpd_net_send_general(NetPath *net_path, const octet_t *buf, int16_t  length)
{
  return ptpd_net_send(buf, length, NULL, &net_path->multicastAddr, net_path->generalPcb);
}

ssize_t ptpd_net_send_peer_general(NetPath *net_path, const octet_t *buf, int16_t  length)
{
  return ptpd_net_send(buf, length, NULL, &net_path->peerMulticastAddr, net_path->generalPcb);
}

#endif
