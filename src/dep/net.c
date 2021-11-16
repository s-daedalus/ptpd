/*-
 * Copyright (c) 2011-2012 George V. Neville-Neil,
 *                         Steven Kreuzer, 
 *                         Martin Burnicki, 
 *                         Jan Breuer,
 *                         Gael Mace, 
 *                         Alexandre Van Kempen,
 *                         Inaqui Delgado,
 *                         Rick Ratzel,
 *                         National Instruments.
 * Copyright (c) 2009-2010 George V. Neville-Neil, 
 *                         Steven Kreuzer, 
 *                         Martin Burnicki, 
 *                         Jan Breuer,
 *                         Gael Mace, 
 *                         Alexandre Van Kempen
 *
 * Copyright (c) 2005-2008 Kendall Correll, Aidan Williams
 *
 * All Rights Reserved
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file   net.c
 * @date   Tue Jul 20 16:17:49 2010
 *
 * @brief  Functions to interact with the network sockets and NIC driver.
 *
 *
 */

#include "../ptpd.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IGMP.h"
#include "ptpd_config.h"
#include "ptp_timestamps.h"
/* choose kernel-level nanoseconds or microseconds resolution on the client-side */


/**
 * shutdown the IPv4 multicast for specific address
 *
 * @param netPath
 * @param multicastAddr
 * 
 * @return TRUE if successful
 */
static Boolean
netShutdownMulticastIPv4(NetPath * netPath, Integer32 multicastAddr)
{
	struct freertos_ip_mreq imr;
	
	/* Close General Multicast */
	imr.imr_multiaddr.sin_addr = multicastAddr;
	imr.imr_interface.sin_addr = netPath->interfaceAddr.sin_addr;

	FreeRTOS_setsockopt(netPath->eventSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_DROP_MEMBERSHIP, 
		   &imr, sizeof(struct freertos_ip_mreq));
	FreeRTOS_setsockopt(netPath->generalSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_DROP_MEMBERSHIP, 
		   &imr, sizeof(struct freertos_ip_mreq));
	
	return TRUE;
}

/**
 * shutdown the multicast (both General and Peer)
 *
 * @param netPath 
 * 
 * @return TRUE if successful
 */
static Boolean
netShutdownMulticast(NetPath * netPath)
{
	/* Close General Multicast */
	netShutdownMulticastIPv4(netPath, netPath->multicastAddr);
	netPath->multicastAddr = 0;

	/* Close Peer Multicast */
	netShutdownMulticastIPv4(netPath, netPath->peerMulticastAddr);
	netPath->peerMulticastAddr = 0;
	
	return TRUE;
}

/* shut down the UDP stuff */
Boolean 
netShutdown(NetPath * netPath)
{
	netShutdownMulticast(netPath);

	netPath->unicastAddr = 0;

	/* Close sockets */
	if (netPath->eventSock > 0)
	FreeRTOS_closesocket(netPath->eventSock);
	netPath->eventSock = -1;

	if (netPath->generalSock > 0)
		FreeRTOS_closesocket(netPath->generalSock);
	netPath->generalSock = -1;

	return TRUE;
}


Boolean
chooseMcastGroup(RunTimeOpts * rtOpts, struct freertos_sockaddr *netAddr)
{
	netAddr->sin_addr = FreeRTOS_inet_addr(DEFAULT_PTP_DOMAIN_ADDRESS);
	if (netAddr->sin_addr == 0) {
		ERROR("failed to encode multicast address: %s\n", addrStr);
		return FALSE;
	}
	return TRUE;
}


/*Test if network layer is OK for PTP*/
UInteger8 
lookupCommunicationTechnology(UInteger8 communicationTechnology)
{
	return PTP_DEFAULT;
}


/**
 * Init the multcast for specific IPv4 address
 * 
 * @param netPath 
 * @param multicastAddr 
 * 
 * @return TRUE if successful
 */
static Boolean
netInitMulticastIPv4(NetPath * netPath, Integer32 multicastAddr)
{
	struct freertos_ip_mreq imr;

	/* multicast send only on specified interface */
	imr.imr_multiaddr.sin_addr = multicastAddr;
	imr.imr_interface.sin_addr = netPath->interfaceAddr.sin_addr;
	/* join multicast group (for receiving) on specified interface */
	if (FreeRTOS_setsockopt(netPath->eventSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_ADD_MEMBERSHIP, 
		       &imr, sizeof(struct freertos_ip_mreq)) < 0
	    || FreeRTOS_setsockopt(netPath->generalSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_ADD_MEMBERSHIP, 
		       &imr, sizeof(struct freertos_ip_mreq)) < 0) {
		PERROR("failed to join the multi-cast group");
		return FALSE;
	}
	return TRUE;
}

/**
 * Init the multcast (both General and Peer)
 * 
 * @param netPath 
 * @param rtOpts 
 * 
 * @return TRUE if successful
 */
Boolean
netInitMulticast(NetPath * netPath,  RunTimeOpts * rtOpts)
{
	struct freertos_sockaddr netAddr;
	char addrStr[NET_ADDRESS_LENGTH];
	
	/* Init General multicast IP address */
	if(!chooseMcastGroup(rtOpts, &netAddr)){
		return FALSE;
	}
	netPath->multicastAddr = netAddr.sin_addr;
	if(!netInitMulticastIPv4(netPath, netPath->multicastAddr)) {
		return FALSE;
	}
	/* End of General multicast Ip address init */

	/* Init Peer multicast IP address */
	netPath->peerMulticastAddr = FreeRTOS_inet_addr(PEER_PTP_DOMAIN_ADDRESS);
	if (netPath->peerMulticastAddr == 0) {
		ERROR("failed to encode multi-cast address: %s\n", addrStr);
		return FALSE;
	}
	
	if(!netInitMulticastIPv4(netPath, netPath->peerMulticastAddr)) {
		return FALSE;
	}
	/* End of Peer multicast Ip address init */
	
	return TRUE;
}

/**
 * Initialize timestamping of packets
 *
 * @param netPath 
 * 
 * @return TRUE if successful
 */
Boolean 
netInitTimestamping(NetPath * netPath)
{
	int val = 1;
	Boolean result = TRUE;
	
#if defined(SO_TIMESTAMPNS) /* Linux, Apple */
	DBG("netInitTimestamping: trying to use SO_TIMESTAMPNS\n");
	
	if (setsockopt(netPath->eventSock, SOL_SOCKET, SO_TIMESTAMPNS, &val, sizeof(int)) < 0
	    || setsockopt(netPath->generalSock, SOL_SOCKET, SO_TIMESTAMPNS, &val, sizeof(int)) < 0) {
		PERROR("netInitTimestamping: failed to enable SO_TIMESTAMPNS");
		result = FALSE;
	}
#elif defined(SO_BINTIME) /* FreeBSD */
	DBG("netInitTimestamping: trying to use SO_BINTIME\n");
		
	if (setsockopt(netPath->eventSock, SOL_SOCKET, SO_BINTIME, &val, sizeof(int)) < 0
	    || setsockopt(netPath->generalSock, SOL_SOCKET, SO_BINTIME, &val, sizeof(int)) < 0) {
		PERROR("netInitTimestamping: failed to enable SO_BINTIME");
		result = FALSE;
	}
#else
	result = FALSE;
#endif
			
/* fallback method */
#if defined(SO_TIMESTAMP) /* Linux, Apple, FreeBSD */
	if (!result) {
		DBG("netInitTimestamping: trying to use SO_TIMESTAMP\n");
		
		if (setsockopt(netPath->eventSock, SOL_SOCKET, SO_TIMESTAMP, &val, sizeof(int)) < 0
		    || setsockopt(netPath->generalSock, SOL_SOCKET, SO_TIMESTAMP, &val, sizeof(int)) < 0) {
			PERROR("netInitTimestamping: failed to enable SO_TIMESTAMP");
			result = FALSE;
		}
		result = TRUE;
	}
#endif

	return result;
}

/**
 * start all of the UDP stuff 
 * must specify 'subdomainName', and optionally 'ifaceName', 
 * if not then pass ifaceName == "" 
 * on socket options, see the 'socket(7)' and 'ip' man pages 
 *
 * @param netPath 
 * @param rtOpts 
 * @param ptpClock 
 * 
 * @return TRUE if successful
 */
Boolean 
netInit(NetPath * netPath, RunTimeOpts * rtOpts, PtpClock * ptpClock)
{
	int temp;
	struct freertos_sockaddr interfaceAddr, netAddr;
	struct freertos_sockaddr addr;

	DBG("netInit\n");

	/* open sockets */
	if ((netPath->eventSock = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP)) < 0
	    || (netPath->generalSock = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP)) < 0) {
		PERROR("failed to initalize sockets");
		return FALSE;
	}
	/* find a network interface */
	interfaceAddr.sin_addr = FreeRTOS_GetIPAddress();
	
	/* save interface address for IGMP refresh */
	netPath->interfaceAddr = interfaceAddr;
	
	DBG("Local IP address used : %s \n", inet_ntoa(interfaceAddr));
	/* bind sockets */
	/*
	 * need INADDR_ANY to allow receipt of multi-cast and uni-cast
	 * messages
	 */
	addr.sin_family = FREERTOS_AF_INET;
	addr.sin_addr = 0; // any address
	addr.sin_port = FreeRTOS_htons(PTP_EVENT_PORT);
	if (FreeRTOS_bind(netPath->eventSock, (struct freertos_sockaddr *)&addr, 
		 sizeof(struct freertos_sockaddr)) < 0) {
		PERROR("failed to bind event socket");
		return FALSE;
	}
	addr.sin_port = FreeRTOS_htons(PTP_GENERAL_PORT);
	if (FreeRTOS_bind(netPath->generalSock, (struct freertos_sockaddr *)&addr, 
		 sizeof(struct freertos_sockaddr)) < 0) {
		PERROR("failed to bind general socket");
		return FALSE;
	}



#ifdef USE_BINDTODEVICE
#ifdef linux
	/*
	 * The following code makes sure that the data is only received on the specified interface.
	 * Without this option, it's possible to receive PTP from another interface, and confuse the protocol.
	 * Calling bind() with the IP address of the device instead of INADDR_ANY does not work.
	 *
	 * More info:
	 *   http://developerweb.net/viewtopic.php?id=6471
	 *   http://stackoverflow.com/questions/1207746/problems-with-so-bindtodevice-linux-socket-option
	 */
#ifdef PTP_EXPERIMENTAL
	if ( !rtOpts->do_hybrid_mode )
#endif
	if (setsockopt(netPath->eventSock, SOL_SOCKET, SO_BINDTODEVICE,
			rtOpts->ifaceName, strlen(rtOpts->ifaceName)) < 0
		|| setsockopt(netPath->generalSock, SOL_SOCKET, SO_BINDTODEVICE,
			rtOpts->ifaceName, strlen(rtOpts->ifaceName)) < 0){
		PERROR("failed to call SO_BINDTODEVICE on the interface");
		return FALSE;
	}
#endif
#endif


	/* send a uni-cast address if specified (useful for testing) */
//	if (rtOpts->unicastAddress[0]) {
//		/* Attempt a DNS lookup first. */
//		struct hostent *host;
//		host = gethostbyname2(rtOpts->unicastAddress, AF_INET);
//                if (host != NULL) {
//			if (host->h_length != 4) {
//				PERROR("unicast host resolved to non ipv4"
//				       "address");
//				return FALSE;
//			}
//			netPath->unicastAddr = 
//				*(uint32_t *)host->h_addr_list[0];
//		} else {
//			/* Maybe it's a dotted quad. */
//			if (!inet_aton(rtOpts->unicastAddress, &netAddr)) {
//				ERROR("failed to encode uni-cast address: %s\n",
//				      rtOpts->unicastAddress);
//				return FALSE;
//				netPath->unicastAddr = netAddr.s_addr;
//			}
//                }
//	} else {
//                netPath->unicastAddr = 0;
//	}

	/* init UDP Multicast on both Default and Pear addresses */
	if (!netInitMulticast(netPath, rtOpts)) {
		return FALSE;
	}

	/* set socket time-to-live to 1 */

	if (FreeRTOS_setsockopt(netPath->eventSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_MULTICAST_TTL, 
		       &rtOpts->ttl, sizeof(int)) < 0
	    || FreeRTOS_setsockopt(netPath->generalSock, FREERTOS_IPPROTO_UDP, FREERTOS_SO_IP_MULTICAST_TTL, 
			  &rtOpts->ttl, sizeof(int)) < 0) {
		PERROR("failed to set the multi-cast time-to-live");
		return FALSE;
	}

	/* enable loopback */
//	temp = 1;
//
//	DBG("Going to set IP_MULTICAST_LOOP with %d \n", temp);
//
//	if (setsockopt(netPath->eventSock, IPPROTO_IP, IP_MULTICAST_LOOP, 
//		       &temp, sizeof(int)) < 0
//	    || setsockopt(netPath->generalSock, IPPROTO_IP, IP_MULTICAST_LOOP, 
//			  &temp, sizeof(int)) < 0) {
//		PERROR("failed to enable multi-cast loopback");
//		return FALSE;
//	}

	/* make timestamps available through recvmsg() */
	if (!netInitTimestamping(netPath)) {
		ERROR("failed to enable receive time stamps");
		return FALSE;
	}

	return TRUE;
}

/*Check if data have been received*/
int 
netSelect(TimeInternal * timeout, NetPath * netPath)
{
	int ret, nfds;
	SocketSet_t readfds;
	//struct timeval tv, *tv_ptr;

	if (timeout < 0)
		return FALSE;
	
	//FD_ZERO(&readfds);
	FreeRTOS_FD_SET(netPath->eventSock, &readfds, eSELECT_READ);
	FreeRTOS_FD_SET(netPath->generalSock, &readfds, eSELECT_READ);

//	if (timeout) {
//		tv.tv_sec = timeout->seconds;
//		tv.tv_usec = timeout->nanoseconds / 1000;
//		tv_ptr = &tv;
//	} else
//		tv_ptr = 0;

//	if (netPath->eventSock > netPath->generalSock)
//		nfds = netPath->eventSock;
//	else
//		nfds = netPath->generalSock;
	ret = FreeRTOS_select(readfds, portMAX_DELAY) > 0;
//	ret = select(nfds + 1, &readfds, 0, 0, tv_ptr) > 0;

	if (ret < 0) {
		if (errno == EAGAIN || errno == EINTR)
			return 0;
	}
	return ret;
}




/** 
 * store received data from network to "buf" , get and store the
 * SO_TIMESTAMP value in "time" for an event message
 *
 * @note Should this function be merged with netRecvGeneral(), below?
 * Jan Breuer: I think that netRecvGeneral should be simplified. Timestamp returned by this
 * function is never used. According to this, netInitTimestamping can be also simplified
 * to initialize timestamping only on eventSock.
 *
 * @param buf 
 * @param time 
 * @param netPath 
 *
 * @return
 */

ssize_t 
netRecvEvent(Octet * buf, TimeInternal * time, NetPath * netPath)
{
	ssize_t ret;
//	struct msghdr msg;
//	struct iovec vec[1];
	struct freertos_sockaddr from_addr;

	//union {
//		struct cmsghdr cm;
//		char	control[CMSG_SPACE(sizeof(struct timeval))];
//	}     cmsg_un;

//	struct cmsghdr *cmsg;

//#if defined(SO_TIMESTAMPNS)
//	struct timespec * ts;
//#elif defined(SO_BINTIME)
//	struct bintime * bt;
//	struct timespec ts;
//#endif
//	
//#if defined(SO_TIMESTAMP)
//	struct timeval * tv;
//#endif
	Boolean timestampValid = FALSE;
	// read the message from the socket and get the corresponding timestamp
	// ####### call CODE TO GET PTP TIMESTAMP HERE#
	ret = FreeRTOS_recvfrom(netPath->eventSock, buf, PACKET_SIZE,0, &from_addr, sizeof(struct freertos_sockaddr));
	timestampValid = retrieve_timestamp(time);
	//vec[0].iov_base = buf;
	//vec[0].iov_len = PACKET_SIZE;
//
	//memset(&msg, 0, sizeof(msg));
	//memset(&from_addr, 0, sizeof(from_addr));
	//memset(buf, 0, PACKET_SIZE);
	//memset(&cmsg_un, 0, sizeof(cmsg_un));
//
	//msg.msg_name = (caddr_t)&from_addr;
	//msg.msg_namelen = sizeof(from_addr);
	//msg.msg_iov = vec;
	//msg.msg_iovlen = 1;
	//msg.msg_control = cmsg_un.control;
	//msg.msg_controllen = sizeof(cmsg_un.control);
	//msg.msg_flags = 0;
//
	//ret = recvmsg(netPath->eventSock, &msg, MSG_DONTWAIT);

//	if (ret <= 0) {
//		if (errno == EAGAIN || errno == EINTR)
//			return 0;
//
//		return ret;
//	}
//	if (msg.msg_flags & MSG_TRUNC) {
//		ERROR("received truncated message\n");
//		return 0;
//	}
//	/* get time stamp of packet */
//	if (!time) {
//		ERROR("null receive time stamp argument\n");
//		return 0;
//	}
//	if (msg.msg_flags & MSG_CTRUNC) {
//		ERROR("received truncated ancillary data\n");
//		return 0;
//	}
//#ifdef PTP_EXPERIMENTAL
//	netPath->lastRecvAddr = from_addr.sin_addr.s_addr;
//#endif
//
//
//
//	if (msg.msg_controllen <= 0) {
//		ERROR("received short ancillary data (%ld/%ld)\n",
//		    (long)msg.msg_controllen, (long)sizeof(cmsg_un.control));
//
//		return 0;
//	}
//
//	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
//	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
//		if (cmsg->cmsg_level == SOL_SOCKET) {
//#if defined(SO_TIMESTAMPNS)
//			if(cmsg->cmsg_type == SCM_TIMESTAMPNS) {
//				ts = (struct timespec *)CMSG_DATA(cmsg);
//				time->seconds = ts->tv_sec;
//				time->nanoseconds = ts->tv_nsec;
//				timestampValid = TRUE;
//				DBGV("kernel NANO recv time stamp %us %dns\n", 
//				     time->seconds, time->nanoseconds);
//				break;
//			}
//#elif defined(SO_BINTIME)
//			if(cmsg->cmsg_type == SCM_BINTIME) {
//				bt = (struct bintime *)CMSG_DATA(cmsg);
//				bintime2timespec(bt, &ts);
//				time->seconds = ts.tv_sec;
//				time->nanoseconds = ts.tv_nsec;
//				timestampValid = TRUE;
//				DBGV("kernel NANO recv time stamp %us %dns\n",
//				     time->seconds, time->nanoseconds);
//				break;
//			}
//#endif
//			
//#if defined(SO_TIMESTAMP)
//			if(cmsg->cmsg_type == SCM_TIMESTAMP) {
//				tv = (struct timeval *)CMSG_DATA(cmsg);
//				time->seconds = tv->tv_sec;
//				time->nanoseconds = tv->tv_usec * 1000;
//				timestampValid = TRUE;
//				DBGV("kernel MICRO recv time stamp %us %dns\n",
//				     time->seconds, time->nanoseconds);
//			}
//#endif
//		}
//	}
//
	if (!timestampValid) {
		/*
		 * do not try to get by with recording the time here, better
		 * to fail because the time recorded could be well after the
		 * message receive, which would put a big spike in the
		 * offset signal sent to the clock servo
		 */
		DBG("netRecvEvent: no receive time stamp\n");
		return 0;
	}

	return ret;
}



/** 
 * 
 * store received data from network to "buf" get and store the
 * SO_TIMESTAMP value in "time" for a general message
 * 
 * @param buf 
 * @param time 
 * @param netPath 
 * 
 * @return 
 */

ssize_t 
netRecvGeneral(Octet * buf, TimeInternal * time, NetPath * netPath)
{
	ssize_t ret;
	//struct msghdr msg;
	//struct iovec vec[1];
	struct freertos_sockaddr from_addr;
	
//	union {
//		struct cmsghdr cm;
//		char	control[CMSG_SPACE(sizeof(struct timeval))];
//	}     cmsg_un;
//	
//	struct cmsghdr *cmsg;
//	
//#if defined(SO_TIMESTAMPNS)
//	struct timespec * ts;
//#elif defined(SO_BINTIME)
//	struct bintime * bt;
//	struct timespec ts;
//#endif
//	
//#if defined(SO_TIMESTAMP)
//	struct timeval * tv;
//#endif
	Boolean timestampValid = FALSE;
	ret = FreeRTOS_recvfrom(netPath->generalSock, buf, PACKET_SIZE,0, &from_addr, sizeof(struct freertos_sockaddr));
	timestampValid = retrieve_timestamp(time);
	
	
//	vec[0].iov_base = buf;
//	vec[0].iov_len = PACKET_SIZE;
//	
//	memset(&msg, 0, sizeof(msg));
//	memset(&from_addr, 0, sizeof(from_addr));
//	memset(buf, 0, PACKET_SIZE);
//	memset(&cmsg_un, 0, sizeof(cmsg_un));
//	
//	msg.msg_name = (caddr_t)&from_addr;
//	msg.msg_namelen = sizeof(from_addr);
//	msg.msg_iov = vec;
//	msg.msg_iovlen = 1;
//	msg.msg_control = cmsg_un.control;
//	msg.msg_controllen = sizeof(cmsg_un.control);
//	msg.msg_flags = 0;
//	
//	ret = recvmsg(netPath->generalSock, &msg, MSG_DONTWAIT);
//	if (ret <= 0) {
//		if (errno == EAGAIN || errno == EINTR)
//			return 0;
//		
//		return ret;
//	}
//	if (msg.msg_flags & MSG_TRUNC) {
//		ERROR("received truncated message\n");
//		return 0;
//	}
//	/* get time stamp of packet */
//	if (!time) {
//		ERROR("null receive time stamp argument\n");
//		return 0;
//	}
//	if (msg.msg_flags & MSG_CTRUNC) {
//		ERROR("received truncated ancillary data\n");
//		return 0;
//	}
//
//#ifdef PTP_EXPERIMENTAL
//	netPath->lastRecvAddr = from_addr.sin_addr.s_addr;
//#endif
//	
//	
//	
//	if (msg.msg_controllen <= 0) {
//		ERROR("received short ancillary data (%ld/%ld)\n",
//		      (long)msg.msg_controllen, (long)sizeof(cmsg_un.control));
//		
//		return 0;
//	}
//	
//	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
//	     cmsg = CMSG_NXTHDR(&msg, cmsg)) {
//		if (cmsg->cmsg_level == SOL_SOCKET) {
//#if defined(SO_TIMESTAMPNS)
//			if(cmsg->cmsg_type == SCM_TIMESTAMPNS) {
//				ts = (struct timespec *)CMSG_DATA(cmsg);
//				time->seconds = ts->tv_sec;
//				time->nanoseconds = ts->tv_nsec;
//				timestampValid = TRUE;
//				DBGV("kernel NANO recv time stamp %us %dns\n", 
//				     time->seconds, time->nanoseconds);
//				break;
//			}
//#elif defined(SO_BINTIME)
//			if(cmsg->cmsg_type == SCM_BINTIME) {
//				bt = (struct bintime *)CMSG_DATA(cmsg);
//				bintime2timespec(bt, &ts);
//				time->seconds = ts.tv_sec;
//				time->nanoseconds = ts.tv_nsec;
//				timestampValid = TRUE;
//				DBGV("kernel NANO recv time stamp %us %dns\n",
//				     time->seconds, time->nanoseconds);
//				break;
//			}
//#endif
//			
//#if defined(SO_TIMESTAMP)
//			if(cmsg->cmsg_type == SCM_TIMESTAMP) {
//				tv = (struct timeval *)CMSG_DATA(cmsg);
//				time->seconds = tv->tv_sec;
//				time->nanoseconds = tv->tv_usec * 1000;
//				timestampValid = TRUE;
//				DBGV("kernel MICRO recv time stamp %us %dns\n",
//				     time->seconds, time->nanoseconds);
//			}
//#endif
//		}
//	}
//
	if (!timestampValid) {
		/*
		 * do not try to get by with recording the time here, better
		 * to fail because the time recorded could be well after the
		 * message receive, which would put a big spike in the
		 * offset signal sent to the clock servo
		 */
		DBG("netRecvGeneral: no receive time stamp\n");
		return 0;
	}

	return ret;
}





//
// alt_dst: alternative destination.
//   if filled, send to this unicast dest;
//   if zero, do the normal operation (send to unicast with -u, or send to the multcast group)
//
///	TODO: (freeRTOS port) we do not use multicast loopback(? at least the sockopt is not set) so we have to make sure the transmit ts get handled correctly
/// TODO: merge these 2 functions into one
///
ssize_t 
netSendEvent(Octet * buf, UInteger16 length, NetPath * netPath, Integer32 alt_dst)
{
	ssize_t ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PTP_EVENT_PORT);

	if (netPath->unicastAddr || alt_dst ) {
		if (netPath->unicastAddr) {
			addr.sin_addr.s_addr = netPath->unicastAddr;
		} else {
			addr.sin_addr.s_addr = alt_dst;
		}

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending uni-cast event message\n");
		/* 
		 * Need to forcibly loop back the packet since
		 * we are not using multicast. 
		 */
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error looping back uni-cast event message\n");
		
	} else {
		addr.sin_addr.s_addr = netPath->multicastAddr;

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending multi-cast event message\n");
	}

	return ret;
}

ssize_t 
netSendGeneral(Octet * buf, UInteger16 length, NetPath * netPath, Integer32 alt_dst)
{
	ssize_t ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PTP_GENERAL_PORT);

	if(netPath->unicastAddr || alt_dst ){
	if (netPath->unicastAddr) {
		addr.sin_addr.s_addr = netPath->unicastAddr;
		} else {
			addr.sin_addr.s_addr = alt_dst;
		}


		ret = sendto(netPath->generalSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending uni-cast general message\n");
	} else {
		addr.sin_addr.s_addr = netPath->multicastAddr;

		ret = sendto(netPath->generalSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending multi-cast general message\n");
	}
	return ret;
}

ssize_t 
netSendPeerGeneral(Octet * buf, UInteger16 length, NetPath * netPath)
{

	ssize_t ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PTP_GENERAL_PORT);

	if (netPath->unicastAddr) {
		addr.sin_addr.s_addr = netPath->unicastAddr;

		ret = sendto(netPath->generalSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending uni-cast general message\n");

	} else {
		addr.sin_addr.s_addr = netPath->peerMulticastAddr;

		ret = sendto(netPath->generalSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending multi-cast general message\n");
	}
	return ret;

}

ssize_t 
netSendPeerEvent(Octet * buf, UInteger16 length, NetPath * netPath)
{
	ssize_t ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(PTP_EVENT_PORT);

	if (netPath->unicastAddr) {
		addr.sin_addr.s_addr = netPath->unicastAddr;

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending uni-cast event message\n");

		/* 
		 * Need to forcibly loop back the packet since
		 * we are not using multicast. 
		 */
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		
		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error looping back uni-cast event message\n");			
	} else {
		addr.sin_addr.s_addr = netPath->peerMulticastAddr;

		ret = sendto(netPath->eventSock, buf, length, 0, 
			     (struct sockaddr *)&addr, 
			     sizeof(struct sockaddr_in));
		if (ret <= 0)
			DBG("error sending multi-cast event message\n");
	}
	return ret;
}



/*
 * refresh IGMP on a timeout
 */
/*
 * @return TRUE if successful
 */
Boolean
netRefreshIGMP(NetPath * netPath, RunTimeOpts * rtOpts, PtpClock * ptpClock)
{
	DBG("netRefreshIGMP\n");
	
	netShutdownMulticast(netPath);
	
	/* suspend process 100 milliseconds, to make sure the kernel sends the IGMP_leave properly */
	usleep(100*1000);

	if (!netInitMulticast(netPath, rtOpts)) {
		return FALSE;
	}
	
	INFO("refreshed IGMP multicast memberships\n");
	return TRUE;
}
