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
#include "mmu/buf_list.h"
#include "mmu/buddy.h"
#include "onps_utils.h"
#include "onps_input.h"
#include "netif/netif.h"

#if SUPPORT_IPV6
#include "ip/ip.h"
#include "ip/icmpv6.h"
#define SYMBOL_GLOBALS
#include "ip/ipv6_configure.h"
#undef SYMBOL_GLOBALS

#if SUPPORT_ETHERNET

static ST_IPv6_DYNADDR l_staIpv6DynAddrs[IPV6_CFG_ADDR_NUM]; //* ipv6动态地址信息存储单元
static ST_IPv6_ROUTER l_staIpv6Routers[IPV6_ROUTER_NUM];     //* ipv6路由器信息存储单元
static CHAR l_bFreeIpv6DynAddrList = -1;  
static CHAR l_bFreeIpv6RouterList = -1; 

static void ipv6_cfg_init(void)
{
	CHAR i; 
	for (i = 0; i < IPV6_CFG_ADDR_NUM - 1; i++)	
		l_staIpv6DynAddrs[i].bNextAddr = i + 1; 
	l_staIpv6DynAddrs[i].bNextAddr = -1; 
	l_bFreeIpv6DynAddrList = 0; 

	for (i = 0; i < IPV6_ROUTER_NUM - 1; i++)
		l_staIpv6Routers[i].bNextRouter = i + 1;
	l_staIpv6Routers[i].bNextRouter = -1;
	l_bFreeIpv6RouterList = 0;
}

PST_IPv6_DYNADDR ipv6_dyn_addr_node_get(EN_ONPSERR *penErr)
{
	PST_IPv6_DYNADDR pstFreeNode = (PST_IPv6_DYNADDR)array_linked_list_get(&l_bFreeIpv6DynAddrList, l_staIpv6DynAddrs, (UCHAR)sizeof(ST_IPv6_DYNADDR), offsetof(ST_IPv6_DYNADDR, bNextAddr)); 
	if (!pstFreeNode)
	{
		if (penErr)
			*penErr = ERRNOIPv6DYNADDRNODE;
	}

	return pstFreeNode; 
}

void ipv6_dyn_addr_node_free(PST_IPv6_DYNADDR pstDynAddrNode)
{
	array_linked_list_put(pstDynAddrNode, &l_bFreeIpv6DynAddrList, l_staIpv6DynAddrs, (UCHAR)sizeof(ST_IPv6_DYNADDR), IPV6_CFG_ADDR_NUM, offsetof(ST_IPv6_DYNADDR, bNextAddr));
}

PST_IPv6_ROUTER ipv6_router_node_get(EN_ONPSERR *penErr)
{
	PST_IPv6_ROUTER pstFreeNode = (PST_IPv6_ROUTER)array_linked_list_get(&l_bFreeIpv6RouterList, l_staIpv6Routers, (UCHAR)sizeof(ST_IPv6_ROUTER), offsetof(ST_IPv6_ROUTER, bNextRouter));
	if (!pstFreeNode)
	{
		if (penErr)
			*penErr = ERRNOIPv6ROUTERNODE;
	}

	return pstFreeNode;
}

void ipv6_router_node_free(PST_IPv6_ROUTER pstRouterNode)
{
	array_linked_list_put(pstRouterNode, &l_bFreeIpv6RouterList, l_staIpv6Routers, (UCHAR)sizeof(ST_IPv6_ROUTER), IPV6_ROUTER_NUM, offsetof(ST_IPv6_ROUTER, bNextRouter));
}

PST_IPv6_DYNADDR ipv6_dyn_addr_get(CHAR bDynAddr)
{
	if (bDynAddr >= 0 && bDynAddr < IPV6_CFG_ADDR_NUM)
		return &l_staIpv6DynAddrs[bDynAddr];
	else
		return NULL; 
}

PST_IPv6_ROUTER ipv6_router_get(CHAR bRouter)
{
	if (bRouter >= 0 && bRouter < IPV6_ROUTER_NUM)
		return &l_staIpv6Routers[bRouter];
	else
		return NULL;
}

void netif_ipv6_dyn_addr_add(PST_NETIF pstNetif, PST_IPv6_DYNADDR pstDynAddr)
{	
	array_linked_list_put(pstDynAddr, &pstNetif->stIPv6.bDynAddr, l_staIpv6DynAddrs, (UCHAR)sizeof(ST_IPv6_DYNADDR), IPV6_CFG_ADDR_NUM, offsetof(ST_IPv6_DYNADDR, bNextAddr));
}

void netif_ipv6_dyn_addr_del(PST_NETIF pstNetif, PST_IPv6_DYNADDR pstDynAddr)
{	
	array_linked_list_del(pstDynAddr, &pstNetif->stIPv6.bDynAddr, l_staIpv6DynAddrs, (UCHAR)sizeof(ST_IPv6_DYNADDR), offsetof(ST_IPv6_DYNADDR, bNextAddr)); 	
}

void netif_ipv6_router_add(PST_NETIF pstNetif, PST_IPv6_ROUTER pstRouter)
{
	array_linked_list_put(pstRouter, &pstNetif->stIPv6.bRouter, l_staIpv6Routers, (UCHAR)sizeof(ST_IPv6_ROUTER), IPV6_ROUTER_NUM, offsetof(ST_IPv6_ROUTER, bNextRouter));
}

void netif_ipv6_router_del(PST_NETIF pstNetif, PST_IPv6_ROUTER pstRouter)
{
	array_linked_list_del(pstRouter, &pstNetif->stIPv6.bRouter, l_staIpv6Routers, (UCHAR)sizeof(ST_IPv6_ROUTER), offsetof(ST_IPv6_ROUTER, bNextRouter));	
}

PST_IPv6_DYNADDR netif_ipv6_dyn_addr_get(PST_NETIF pstNetif, CHAR *pbNextAddr)
{
	return (PST_IPv6_DYNADDR)array_linked_list_get_next(pbNextAddr, &pstNetif->stIPv6.bDynAddr, l_staIpv6DynAddrs, (UCHAR)sizeof(ST_IPv6_DYNADDR), offsetof(ST_IPv6_DYNADDR, bNextAddr)); 
}

PST_IPv6_ROUTER netif_ipv6_router_get(PST_NETIF pstNetif, CHAR *pbNextRouter)
{
	return (PST_IPv6_ROUTER)array_linked_list_get_next(pbNextRouter, &pstNetif->stIPv6.bRouter, l_staIpv6Routers, (UCHAR)sizeof(ST_IPv6_ROUTER), offsetof(ST_IPv6_ROUTER, bNextRouter)); 
}

//* icmpv6支持的无状态(stateless)地址配置定时器溢出函数
static void ipv6_cfg_timeout_handler(void *pvParam)
{
	PST_NETIF pstNetif = (PST_NETIF)pvParam; 
}

//* 地址冲突检测（DAD）计时函数
static void ipv6_cfg_dad_timeout_handler(void *pvParam)
{
	PST_NETIF pstNetif; 

	PST_IPv6_DYNADDR pstTentAddr = (PST_IPv6_DYNADDR)pvParam;
	UCHAR *pubAddr = (UCHAR *)pvParam; 
	if (IPv6LNKADDR_FLAG == pubAddr[15])
	{		
		PST_IPv6 pstIpv6 = (PST_IPv6)(pubAddr - offsetof(ST_IPv6, stLnkAddr));
		pstNetif = (PST_NETIF)((UCHAR *)pstIpv6 - offsetof(ST_NETIF, stIPv6)); 
	}
	else
	{		
		PST_IPv6_ROUTER pstRouter = (PST_IPv6_ROUTER)ipv6_router_get((CHAR)pstTentAddr->bitRouter);
		if (pstRouter)		
			pstNetif = pstRouter->pstNetif;					
		else
		{
	#if SUPPORT_PRINTF && DEBUG_LEVEL
		#if PRINTF_THREAD_MUTEX
			os_thread_mutex_lock(o_hMtxPrintf);
		#endif
			printf("ipv6_cfg_dad_timeout_handler() failed, router index out of range (0 - %d): %d\r\n", IPV6_ROUTER_NUM, pstTentAddr->bitRouter);
		#if PRINTF_THREAD_MUTEX
			os_thread_mutex_unlock(o_hMtxPrintf);
		#endif
	#endif
			return; 
		}
	}

	//* 整套算法得以正常工作的基础是ST_IPv6_LNKADDR与ST_IPv6_DYNADDR的头部字段位长于存储顺序完全一致，覆盖结构体前17个字节
	switch (pstTentAddr->bitState)
	{
	case IPv6ADDR_TENTATIVE:
		pstTentAddr->bitTimingCnt++;
		if (pstTentAddr->bitTimingCnt < IPv6_DAD_TIMEOUT)
		{
			//* 存在冲突则重新生成地址继续试探
			if (pstTentAddr->bitConflict)
			{
				//* 重新生成尾部地址再次进行DAD
				UINT unNewTailAddr = rand_big();
				memcpy(pubAddr + 13, (UCHAR *)&unNewTailAddr, 3);
				pstTentAddr->bitTimingCnt = 0;
				pstTentAddr->bitConflict = FALSE;
			}

			icmpv6_send_ns(pstNetif, NULL, pstTentAddr->ubaAddr, NULL);   //* 继续发送试探报文，确保所有节点都能收到
			one_shot_timer_new(ipv6_cfg_dad_timeout_handler, pvParam, 1); //* 再次开启定时器
		}
		else
		{
			pstTentAddr->bitState = IPv6ADDR_PREFERRED;
			
			//* 如果是动态地址配置成功后需要挂接到网卡上并开始生存周期倒计时
			if (IPv6LNKADDR_FLAG != pubAddr[15])
				ipv6_dyn_addr_add_to_netif(pstNetif, pstTentAddr);
		}

		break;

	default:
		return;
	}
}

//* 开始ipv6地址自动配置
BOOL ipv6_cfg_start(PST_NETIF pstNetif, EN_ONPSERR *penErr)
{
	//* 生成试探性的链路本地地址（Tentative Link Local Address）
	//icmpv6_lnk_addr_get(pstNetif, pstNetif->stIPv6.stLnkAddr.ubaAddr); 

	//* 测试ipv6链路本地地址冲突逻辑用，使用网络其它节点已经成功配置的地址来验证协议栈DAD逻辑是否正确
	memcpy(pstNetif->stIPv6.stLnkAddr.ubaAddr, "\xfe\x80\x00\x00\x00\x00\x00\x00\xdd\x62\x1a\x01\xfa\xa0\xd0\xe3", 16); 	

	//* 初始地址状态为“试探”（TENTATIVE）
	pstNetif->stIPv6.stLnkAddr.bitState = IPv6ADDR_TENTATIVE;

	//* 开启one-shot定时器，步长：1秒	
	if (one_shot_timer_new(ipv6_cfg_timeout_handler, pstNetif, 1))			
		pstNetif->stIPv6.stLnkAddr.bitTimingCnt = 0; 
	else
	{
		if (penErr)
			*penErr = ERRNOIDLETIMER;
		return FALSE; 
	}

	//* 显式地通知后续的处理函数这是链路本地地址不是根据路由器发布的前缀生成或dhcpv6服务器分配的动态地址
	pstNetif->stIPv6.stLnkAddr.ubaAddr[15] = IPv6LNKADDR_FLAG;	
	return ipv6_cfg_dad(pstNetif, &pstNetif->stIPv6.stLnkAddr, penErr);	
}

//* 开启重复地址检测
BOOL ipv6_cfg_dad(PST_NETIF pstNetif, void *pstTentAddr, EN_ONPSERR *penErr)
{
	//* 接下来要操作的字段PST_IPv6_LNKADDR与PST_IPv6_DYNADDR存储位置完全相同，所以这里直接使用其中一个作为参数pstTentAddr的确定的数据类型
	PST_IPv6_DYNADDR pstDynAddr = (PST_IPv6_DYNADDR)pstTentAddr;
	pstDynAddr->bitState = IPv6ADDR_TENTATIVE;
	pstDynAddr->bitConflict = FALSE;
	pstDynAddr->bitTimingCnt = 0;

	//* 开启DAD检测定时器，步长：1秒	
	if (!one_shot_timer_new(ipv6_cfg_dad_timeout_handler, pstTentAddr, 1))
	{
		if (penErr)
			*penErr = ERRNOIDLETIMER;
		return FALSE;
	}

	//* 发送邻居节点请求报文开启重复地址检测DAD（Duplicate Address Detect）以确定这个试探地址没有被其它节点使用
	if (icmpv6_send_ns(pstNetif, NULL, pstDynAddr->ubaAddr, penErr) > 0)
		return TRUE;
	else
		return FALSE;
}
#endif
#endif