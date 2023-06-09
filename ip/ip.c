/*
 * 版权属于onps栈开发团队，遵循Apache License 2.0开源许可协议
 *
 */
#include "port/datatype.h"
#include "port/sys_config.h"
#include "onps_errors.h"
#include "port/os_datatype.h"
#include "port/os_adapter.h"
#include "one_shot_timer.h"
#include "mmu/buddy.h"
#include "mmu/buf_list.h"
#include "onps_utils.h"
#include "netif/netif.h"
#include "netif/route.h"
#include "ethernet/arp.h"

#define SYMBOL_GLOBALS
#include "ip/ip.h"
#undef SYMBOL_GLOBALS
#include "ethernet/ethernet.h"
#include "ip/icmp.h"
#if SUPPORT_IPV6
#include "ip/icmpv6.h"
#include "ip/ipv6_configure.h"
#endif
#include "ip/tcp.h"
#include "ip/udp.h"

//* 必须严格按照EN_NPSPROTOCOL类型定义的顺序指定IP上层协议值
static const EN_IPPROTO lr_enaIPProto[] = {
    IPPROTO_MAX,
    IPPROTO_MAX,
    IPPROTO_MAX,
    IPPROTO_MAX,
#if SUPPORT_IPV6
    IPPROTO_MAX,
#endif
    IPPROTO_MAX,
#if SUPPORT_IPV6
    IPPROTO_MAX,
#endif
    IPPROTO_ICMP, 
#if SUPPORT_IPV6
	IPPROTO_ICMPv6,
#endif
    IPPROTO_MAX,
    IPPROTO_TCP, 
    IPPROTO_UDP
};
static USHORT l_usIPIdentifier = 0; 

static INT netif_ip_send(PST_NETIF pstNetif, UCHAR *pubDstMacAddr, in_addr_t unSrcAddr, in_addr_t unDstAddr, in_addr_t unArpDstAddr, EN_NPSPROTOCOL enProtocol, UCHAR ubTTL, SHORT sBufListHead, EN_ONPSERR *penErr)
{
    INT nRtnVal; 
    BOOL blNetifFreedEn = TRUE; 

    os_critical_init();

    ST_IP_HDR stHdr;
    stHdr.bitHdrLen = sizeof(ST_IP_HDR) / sizeof(UINT); //* IP头长度，单位：UINT
    stHdr.bitVer = 4; //* IPv4
    stHdr.bitMustBeZero = 0;
    stHdr.bitTOS = 0; //* 一般服务
    stHdr.bitPrior = 0;
    stHdr.usPacketLen = htons(sizeof(ST_IP_HDR) + (USHORT)buf_list_get_len(sBufListHead));
    os_enter_critical();
    {
        stHdr.usID = htons(l_usIPIdentifier);
        l_usIPIdentifier++;
    }
    os_exit_critical();
    stHdr.bitFragOffset0 = 0;
    stHdr.bitFlag = 1 << 1;  //* Don't fragment
    stHdr.bitFragOffset1 = 0;
    stHdr.ubTTL = ubTTL;
    stHdr.ubProto = (UCHAR)lr_enaIPProto[enProtocol];
    stHdr.usChecksum = 0;
    stHdr.unSrcIP = unSrcAddr/*pstNetif->stIPv4.unAddr*/;
    stHdr.unDstIP = htonl(unDstAddr);

    //* 挂载到buf list头部
    SHORT sHdrNode;
    sHdrNode = buf_list_get_ext((UCHAR *)&stHdr, (USHORT)sizeof(ST_IP_HDR), penErr);
    if (sHdrNode < 0)
    {
        //* 使用计数减一，释放对网卡资源的占用
        netif_freed(pstNetif);
        return -1;
    }
    buf_list_put_head(&sBufListHead, sHdrNode);

    //* 计算校验和
    stHdr.usChecksum = tcpip_checksum((USHORT *)&stHdr, sizeof(ST_IP_HDR))/*tcpip_checksum_ext(sBufListHead)*/;

#if SUPPORT_ETHERNET
    //* 看看选择的网卡是否是ethernet类型，如果是则首先需要在此获取目标mac地址
    if (NIF_ETHERNET == pstNetif->enType)
    {
		if (pubDstMacAddr)
		{
			nRtnVal = pstNetif->pfunSend(pstNetif, IPV4, sBufListHead, pubDstMacAddr, penErr);
		}        
		else
		{
			UCHAR ubaDstMac[ETH_MAC_ADDR_LEN];            
			nRtnVal = arp_get_mac_ext(pstNetif, unSrcAddr, unArpDstAddr, ubaDstMac, sBufListHead, &blNetifFreedEn, penErr); 
			if (!nRtnVal) //* 存在该条目，则直接调用ethernet接口注册的发送函数即可			
				nRtnVal = pstNetif->pfunSend(pstNetif, IPV4, sBufListHead, ubaDstMac, penErr);			
		}        
    }
    else
    {
#endif
        //* 完成发送
        nRtnVal = pstNetif->pfunSend(pstNetif, IPV4, sBufListHead, NULL, penErr);
#if SUPPORT_ETHERNET
    }    
#endif

    //* 如果不需要等待arp查询结果，则立即释放对网卡的使用权
    if (blNetifFreedEn)
        netif_freed(pstNetif); 

    //* 释放刚才申请的buf list节点
    buf_list_free(sHdrNode);

    return nRtnVal;
}

INT ip_send(PST_NETIF pstNetif, UCHAR *pubDstMacAddr, in_addr_t unSrcAddr, in_addr_t unDstAddr, EN_NPSPROTOCOL enProtocol, UCHAR ubTTL, SHORT sBufListHead, EN_ONPSERR *penErr)
{
	in_addr_t unSrcAddrUsed = unSrcAddr, unArpDstAddr = htonl(unDstAddr);

	//* 如果未指定netif则通过路由表选择一个适合的netif
	PST_NETIF pstNetifUsed = pstNetif; 
    if (pstNetifUsed)
		netif_used(pstNetifUsed);
	else
    {
        pstNetifUsed = route_get_netif(unArpDstAddr/*unDstAddr*/, TRUE, &unSrcAddrUsed, &unArpDstAddr);
        if (NULL == pstNetifUsed)
        {
            if (penErr)
                *penErr = ERRADDRESSING;

            return -1;
        }
    }
        
    return netif_ip_send(pstNetifUsed, pubDstMacAddr, unSrcAddrUsed, unDstAddr, unArpDstAddr, enProtocol, ubTTL, sBufListHead, penErr);
}

INT ip_send_ext(in_addr_t unSrcAddr, in_addr_t unDstAddr, EN_NPSPROTOCOL enProtocol, UCHAR ubTTL, SHORT sBufListHead, EN_ONPSERR *penErr)
{
    in_addr_t unRouteSrcAddr, unArpDstAddr = htonl(unDstAddr);

    PST_NETIF pstNetif = route_get_netif(unArpDstAddr/*unDstAddr*/, TRUE, &unRouteSrcAddr, &unArpDstAddr);
    if (NULL == pstNetif)
    {
        if (penErr)
            *penErr = ERRADDRESSING;

        return -1;
    }

    //* 再次寻址与上层协议寻址结果不一致，则直接放弃该报文
    if (unSrcAddr != unRouteSrcAddr)
    {
        netif_freed(pstNetif); 

        if (penErr)
            *penErr = ERRROUTEADDRMATCH;

        return -1; 
    }

    /* 
    PST_NETIF pstNetif = netif_get_by_ip(unSrcAddr, TRUE);
    if (NULL == pstNetif)
    {
        if (penErr)
            *penErr = ERRNONETIFFOUND;

        return -1;
    }
    */

    return netif_ip_send(pstNetif, NULL, unSrcAddr, unDstAddr, unArpDstAddr, enProtocol, ubTTL, sBufListHead, penErr);
}

void ip_recv(PST_NETIF pstNetif, UCHAR *pubDstMacAddr, UCHAR *pubPacket, INT nPacketLen)
{
    PST_IP_HDR pstHdr = (PST_IP_HDR)pubPacket;
    UCHAR usHdrLen = pstHdr->bitHdrLen * 4;
	USHORT usIpPacketLen = htons(pstHdr->usPacketLen);
	if (nPacketLen >= (INT)usIpPacketLen)
		nPacketLen = (INT)usIpPacketLen;
	else //* 指定的报文长度与实际收到的字节数不匹配，直接丢弃该报文
		return; 

    //* 首先看看校验和是否正确
    USHORT usPktChecksum = pstHdr->usChecksum;
    pstHdr->usChecksum = 0;
    USHORT usChecksum = tcpip_checksum((USHORT *)pubPacket, usHdrLen);
    if (usPktChecksum != usChecksum)
    {
#if SUPPORT_PRINTF && DEBUG_LEVEL > 3
        pstHdr->usChecksum = usPktChecksum;
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_lock(o_hMtxPrintf);
    #endif
        printf("checksum error (%04X, %04X), and the IP packet will be dropped\r\n", usChecksum, usPktChecksum);
        printf_hex(pubPacket, nPacketLen, 48);
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_unlock(o_hMtxPrintf);
    #endif
#endif
        return; 
    }

#if SUPPORT_ETHERNET
    //* 如果当前网卡类型是ethernet网卡，就需要看看ip地址是否匹配，只有匹配的才会处理		
    if (NIF_ETHERNET == pstNetif->enType)
    {
		// 注意，这里仅支持255.255.255.255这样的广播报文，x.x.x.255类型的广播报文不被支持
		if (pstHdr->unDstIP != 0xFFFFFFFF)
		{
			// ip地址不匹配，直接丢弃当前报文
			if (pstNetif->stIPv4.unAddr && !ethernet_ipv4_addr_matched(pstNetif, pstHdr->unDstIP))
				return;

			// 更新arp缓存表
			PST_NETIFEXTRA_ETH pstExtra = (PST_NETIFEXTRA_ETH)pstNetif->pvExtra;
			arp_add_ethii_ipv4_ext(pstExtra->pstcbArp->staEntry, pstHdr->unSrcIP, pubDstMacAddr); 
		}        
    }	
#endif
    

    switch (pstHdr->ubProto)
    {
    case IPPROTO_ICMP: 
        icmp_recv(pstNetif, pubDstMacAddr, pubPacket, nPacketLen);
        break; 

    case IPPROTO_TCP:
        tcp_recv(pstHdr->unSrcIP, pstHdr->unDstIP, pubPacket + usHdrLen, nPacketLen - usHdrLen);
        break; 

    case IPPROTO_UDP:
        udp_recv(pstHdr->unSrcIP, pstHdr->unDstIP, pubPacket + usHdrLen, nPacketLen - usHdrLen);
        break;

    default:
#if SUPPORT_PRINTF && DEBUG_LEVEL > 3
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_lock(o_hMtxPrintf);
    #endif
        printf("unsupported IP upper layer protocol (%d), the packet will be dropped\r\n", (UINT)pstHdr->ubProto);
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_unlock(o_hMtxPrintf);
    #endif
#endif
        break; 
    }
}

#if SUPPORT_IPV6
static INT netif_ipv6_send(PST_NETIF pstNetif, UCHAR *pubDstMacAddr, UCHAR ubaSrcIpv6[16], UCHAR ubaDstIpv6[16], UCHAR ubaDstIpv6ToMac[16], UCHAR ubNextHeader, SHORT sBufListHead, UINT unFlowLabel, UCHAR ubHopLimit, EN_ONPSERR *penErr)
{
	INT nRtnVal;
	BOOL blNetifFreedEn = TRUE; 

	ST_IPv6_HDR stHdr; 
	stHdr.ipv6_ver = 6; 
	stHdr.ipv6_dscp = 0; 
	stHdr.ipv6_ecn = 0;
	stHdr.ipv6_flow_label = unFlowLabel & 0x000FFFFF; 
	stHdr.ipv6_flag = htonl(stHdr.ipv6_flag); 
	stHdr.usPayloadLen = htons((USHORT)buf_list_get_len(sBufListHead)); 
	stHdr.ubNextHdr = ubNextHeader; 
	stHdr.ubHopLimit = ubHopLimit; 
	memcpy(stHdr.ubaSrcIpv6, ubaSrcIpv6, 16);
	memcpy(stHdr.ubaDstIpv6, ubaDstIpv6, 16); 

	//* 挂载到buf list头部
	SHORT sHdrNode;
	sHdrNode = buf_list_get_ext((UCHAR *)&stHdr, (USHORT)sizeof(ST_IPv6_HDR), penErr);
	if (sHdrNode < 0)
	{
		//* 使用计数减一，释放对网卡资源的占用
		netif_freed(pstNetif);
		return -1;
	}
	buf_list_put_head(&sBufListHead, sHdrNode); 	

#if SUPPORT_ETHERNET
	//* 如果网络接口类型为ethernet，需要先获取目标mac地址
	if (NIF_ETHERNET == pstNetif->enType)
	{
		if (pubDstMacAddr)
		{
			nRtnVal = pstNetif->pfunSend(pstNetif, IPV6, sBufListHead, pubDstMacAddr, penErr);
		}
		else
		{
			UCHAR ubaDstMac[ETH_MAC_ADDR_LEN];			          
			nRtnVal = ipv6_mac_get_ext(pstNetif, ubaSrcIpv6, ubaDstIpv6ToMac, ubaDstMac, sBufListHead, &blNetifFreedEn, penErr);
			if (!nRtnVal) //* 得到目标mac地址，直接发送该报文
				nRtnVal = pstNetif->pfunSend(pstNetif, IPV6, sBufListHead, ubaDstMac, penErr);
		}
	}
	else
	{ 
#endif
		//* 完成发送
		nRtnVal = pstNetif->pfunSend(pstNetif, IPV6, sBufListHead, NULL, penErr);
#if SUPPORT_ETHERNET
	}
#endif

	//* 如果不需要等待icmpv6查询结果，则立即释放对网卡的使用权
	if (blNetifFreedEn)
		netif_freed(pstNetif);

	//* 释放刚才申请的buf list节点
	buf_list_free(sHdrNode);

	return nRtnVal;
}

INT ipv6_send(PST_NETIF pstNetif, UCHAR *pubDstMacAddr, UCHAR ubaSrcIpv6[16], UCHAR ubaDstIpv6[16], UCHAR ubNextHeader, SHORT sBufListHead, UINT unFlowLabel, UCHAR ubHopLimit, EN_ONPSERR *penErr)
{
	UCHAR ubaSrcIpv6Used[16], ubaDstIpv6ToMac[16]; 
	memcpy(ubaSrcIpv6Used, ubaSrcIpv6, 16); 
	memcpy(ubaDstIpv6ToMac, ubaDstIpv6, 16); 

	//* 如果未指定netif则通过路由表选择一个适合的netif
	PST_NETIF pstNetifUsed = pstNetif; 
	if (pstNetifUsed)
		netif_used(pstNetifUsed);
	else
	{		
		pstNetifUsed = route_ipv6_get_netif(ubaDstIpv6ToMac, TRUE, ubaSrcIpv6Used, ubaDstIpv6ToMac); 
		if (NULL == pstNetifUsed)
		{
			if (penErr)
				*penErr = ERRADDRESSING;

			return -1;
		}
	}

	return netif_ipv6_send(pstNetifUsed, pubDstMacAddr, ubaSrcIpv6Used, ubaDstIpv6, ubaDstIpv6ToMac, ubNextHeader, sBufListHead, unFlowLabel, ubHopLimit, penErr);
}

INT ipv6_send_ext(UCHAR ubaSrcIpv6[16], UCHAR ubaDstIpv6[16], UCHAR ubNextHeader, SHORT sBufListHead, UINT unFlowLabel, UCHAR ubHopLimit, EN_ONPSERR *penErr)
{
	UCHAR ubaSrcIpv6Used[16], ubaDstIpv6ToMac[16];	
	memcpy(ubaDstIpv6ToMac, ubaDstIpv6, 16);
	     
	//* 路由寻址
	PST_NETIF pstNetif = route_ipv6_get_netif(ubaDstIpv6ToMac, TRUE, ubaSrcIpv6Used, ubaDstIpv6ToMac); 
	if (NULL == pstNetif)
	{
		if (penErr)
			*penErr = ERRADDRESSING;

		return -1;
	}

	//* 路由寻址结果与上层调用函数给出的源地址不一致，则直接报错并抛弃该报文
	if (memcmp(ubaSrcIpv6, ubaSrcIpv6Used, 16))
	{
		netif_freed(pstNetif);

		if (penErr)
			*penErr = ERRROUTEADDRMATCH;

		return -1;
	}
	     
	return netif_ipv6_send(pstNetif, NULL, ubaSrcIpv6, ubaDstIpv6, ubaDstIpv6ToMac, ubNextHeader, sBufListHead, unFlowLabel, ubHopLimit, penErr);
}

void ipv6_recv(PST_NETIF pstNetif, UCHAR *pubDstMacAddr, UCHAR *pubPacket, INT nPacketLen)
{
	if (pstNetif->stIPv6.bitCfgState < IPv6CFG_LNKADDR)
		return; 


	PST_IPv6_HDR pstHdr = (PST_IPv6_HDR)pubPacket;	
	USHORT usPayloadLen = htons(pstHdr->usPayloadLen); 
	if (nPacketLen < (INT)usPayloadLen) //* 指定的报文长度与实际收到的字节数不匹配，直接丢弃该报文
		return; 

#if SUPPORT_ETHERNET
	//* 如果网络接口类型为ethernet，就需要看看ipv6地址是否匹配，只有匹配的才会处理，同时顺道更新ipv6 mac地址映射缓存表
	if (NIF_ETHERNET == pstNetif->enType)
	{
		CHAR szIpv6[40];
		os_thread_mutex_lock(o_hMtxPrintf);
		printf("%s -> ", inet6_ntoa(pstHdr->ubaSrcIpv6, szIpv6));
		printf("%s\r\n", inet6_ntoa(pstHdr->ubaDstIpv6, szIpv6));
		os_thread_mutex_unlock(o_hMtxPrintf);

		// ip地址不匹配，直接丢弃当前报文
		if (!ethernet_ipv6_addr_matched(pstNetif, pstHdr->ubaDstIpv6))
			return; 
	}
#endif

	if (pstHdr->ubNextHdr)
	{
		switch (pstHdr->ubNextHdr)
		{
		case IPPROTO_ICMPv6:
			icmpv6_recv(pstNetif, pubDstMacAddr, pubPacket, nPacketLen, pubPacket + sizeof(ST_IPv6_HDR));
			break;

		case IPPROTO_TCP:
			//tcp_recv(pstHdr->unSrcIP, pstHdr->unDstIP, pubPacket + usHdrLen, nPacketLen - usHdrLen);
			break;

		case IPPROTO_UDP:
			//udp_recv(pstHdr->unSrcIP, pstHdr->unDstIP, pubPacket + usHdrLen, nPacketLen - usHdrLen);
			break;

		default:
	#if SUPPORT_PRINTF && DEBUG_LEVEL > 3
		#if PRINTF_THREAD_MUTEX
			os_thread_mutex_lock(o_hMtxPrintf);
		#endif
			printf("unsupported IPv6 upper layer protocol (%d), the packet will be dropped\r\n", (UINT)pstHdr->ubNextHdr);
		#if PRINTF_THREAD_MUTEX
			os_thread_mutex_unlock(o_hMtxPrintf);
		#endif
	#endif
			break;
		}
	}
	else //* 处理ipv6逐跳选项（IPv6 Hop-by-Hop Option）
	{

	}	
}
#endif
