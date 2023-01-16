/*
 * 版权属于onps栈开发团队，遵循Apache License 2.0开源许可协议
 *
 */
#include "port/datatype.h"
#include "onps_errors.h"
#include "port/sys_config.h"
#include "port/os_datatype.h"
#include "port/os_adapter.h"
#include "one_shot_timer.h"
#include "mmu/buddy.h"
#include "mmu/buf_list.h"
#include "onps_utils.h"
#include "netif/netif.h"
#include "netif/route.h"
#include "onps_input.h"
#include "ip/tcp_frame.h"
#include "ip/tcp_options.h" 
#define SYMBOL_GLOBALS
#include "ip/tcp.h"
#undef SYMBOL_GLOBALS

static void tcp_send_fin(PST_TCPLINK pstLink); 
static void tcpsrv_send_syn_ack_with_start_timer(PST_TCPLINK pstLink, in_addr_t unSrcAddr, USHORT usSrcPort, in_addr_t unDstAddr, USHORT usDstPort); 

void tcpsrv_syn_recv_timeout_handler(void *pvParam)
{
    PST_TCPLINK pstLink = (PST_TCPLINK)pvParam;

    //* 是否在溢出的时候收到应答
    if (!pstLink->stcbWaitAck.bIsAcked)
    {
        if (pstLink->stcbWaitAck.bRcvTimeout < 32)
        {
            pstLink->stcbWaitAck.bRcvTimeout *= 2; 
            tcpsrv_send_syn_ack_with_start_timer(pstLink, pstLink->stLocal.pstAddr->unNetifIp, pstLink->stLocal.pstAddr->usPort, pstLink->stPeer.stAddr.unIp, pstLink->stPeer.stAddr.usPort);
        }
        else
        {
            onps_input_free(pstLink->stcbWaitAck.nInput); 

    #if SUPPORT_PRINTF && DEBUG_LEVEL > 1
            CHAR szAddr[20], szAddrClt[20];
        #if PRINTF_THREAD_MUTEX
            os_thread_mutex_lock(o_hMtxPrintf);
        #endif
            printf("The tcp server@%s:%d waits for the client's syn ack to time out, and the current link will be closed (cleint@%s:%d)\r\n", 
                        inet_ntoa_safe_ext(pstLink->stLocal.pstAddr->unNetifIp, szAddr), pstLink->stLocal.pstAddr->usPort, inet_ntoa_safe_ext(pstLink->stPeer.stAddr.unIp, szAddrClt), pstLink->stPeer.stAddr.usPort);
        #if PRINTF_THREAD_MUTEX
            os_thread_mutex_unlock(o_hMtxPrintf);
        #endif
    #endif
        }
    }
}

static void tcp_ack_timeout_handler(void *pvParam)
{        
    PST_TCPLINK pstLink = (PST_TCPLINK)pvParam;     	

    if (!pstLink->stcbWaitAck.bIsAcked)
    {
        if (TLSCONNECTED == pstLink->bState)
        {
			if (TDSSENDING == pstLink->stLocal.bDataSendState)
				pstLink->stLocal.bDataSendState = (CHAR)TDSTIMEOUT;			
        }
        else
            pstLink->bState = TLSACKTIMEOUT;       		

		if (pstLink->stcbWaitAck.bRcvTimeout)					
			onps_input_sem_post(pstLink->stcbWaitAck.nInput);
    }

	pstLink->stcbWaitAck.pstTimer = NULL; 
}

static void tcp_close_timeout_handler(void *pvParam)
{
    PST_TCPLINK pstLink = (PST_TCPLINK)pvParam;
    INT nRtnVal; 

    switch ((EN_TCPLINKSTATE)pstLink->bState)
    {
    case TLSFINWAIT1: 
        nRtnVal = onps_input_tcp_close_time_count(pstLink->stcbWaitAck.nInput);
        if (nRtnVal == 1)        
            tcp_send_fin(pstLink);
        else if (nRtnVal == 2) //*一直没收到对端的ACK报文，直接进入FIN_WAIT2态        
            onps_input_set_tcp_close_state(pstLink->stcbWaitAck.nInput, TLSFINWAIT2); 
        else; 
        break; 

    case TLSFINWAIT2:
        nRtnVal = onps_input_tcp_close_time_count(pstLink->stcbWaitAck.nInput);        
        if (nRtnVal == 2) //* 一直未收到对端发送的FIN报文        
            onps_input_set_tcp_close_state(pstLink->stcbWaitAck.nInput, TLSTIMEWAIT);
        else; 
        break; 

    case TLSCLOSING:
        nRtnVal = onps_input_tcp_close_time_count(pstLink->stcbWaitAck.nInput);
        if (nRtnVal == 2) //* 一直未收到对端发送的FIN报文        
            onps_input_set_tcp_close_state(pstLink->stcbWaitAck.nInput, TLSTIMEWAIT); 
        else;         
        break; 

    case TLSTIMEWAIT:             
        nRtnVal = onps_input_tcp_close_time_count(pstLink->stcbWaitAck.nInput);         
        if (nRtnVal == 1)
        {
            if (pstLink->bIsPassiveFin)
                tcp_send_fin(pstLink);
        }
        if (nRtnVal == 2) //* 超时，则FIN操作结束，释放input资源
        {           
            onps_input_free(pstLink->stcbWaitAck.nInput);             
            return; 
        }
        else;        
        break;

    case TLSCLOSED:         
        //* FIN操作结束，释放input资源
        onps_input_free(pstLink->stcbWaitAck.nInput);        
        return;  
    }       

    //* 重新启动定时器
    pstLink->stcbWaitAck.pstTimer = one_shot_timer_new(tcp_close_timeout_handler, pstLink, 1);     
}

static INT tcp_send_packet(PST_TCPLINK pstLink, in_addr_t unSrcAddr, USHORT usSrcPort, in_addr_t unDstAddr, USHORT usDstPort,
                            UNI_TCP_FLAG uniFlag, UCHAR *pubOptions, USHORT usOptionsBytes, UCHAR *pubData, USHORT usDataBytes, 
#if SUPPORT_SACK
    BOOL blIsSpecSeqNum, UINT unSeqNum, 
#endif
    EN_ONPSERR *penErr)
{
    //* 挂载用户数据
    SHORT sBufListHead = -1; 
    SHORT sDataNode = -1; 
    if (pubData)
    {        
        sDataNode = buf_list_get_ext(pubData, (UINT)usDataBytes, penErr); 
        if (sDataNode < 0)
            return -1;
        buf_list_put_head(&sBufListHead, sDataNode);
    }

    //* 挂载tcp options选项
    SHORT sOptionsNode = -1; 
    if (usOptionsBytes)
    {
        sOptionsNode = buf_list_get_ext(pubOptions, (UINT)usOptionsBytes, penErr);
        if (sOptionsNode < 0)
        {
            if (sDataNode >= 0)
                buf_list_free(sDataNode);
            return -1;
        }
        buf_list_put_head(&sBufListHead, sOptionsNode);
    }

    //* 要确保本地Sequence Number和对端Sequence Number不乱序就必须加锁，因为tcp接收线程与发送线程并不属于同一个，因为线程优先级问题导致发送线程在发送前一刻被接收线程强行打断并率先发送了
    //* 应答报文，此时序号有可能已大于发送线程携带的序号，乱序问题就此产生
    onps_input_lock(pstLink->stcbWaitAck.nInput); 

    //* 填充tcp头
    ST_TCP_HDR stHdr; 
    stHdr.usSrcPort = htons(usSrcPort);
    stHdr.usDstPort = htons(usDstPort);
#if SUPPORT_SACK
    if (blIsSpecSeqNum)
        stHdr.unSeqNum = htonl(unSeqNum); 
    else
        stHdr.unSeqNum = htonl(pstLink->stLocal.unHasSndBytes + 1);
#else
    stHdr.unSeqNum = htonl(pstLink->stLocal.unSeqNum);
#endif
    stHdr.unAckNum = htonl(pstLink->stPeer.unSeqNum);
    uniFlag.stb16.hdr_len = (UCHAR)(sizeof(ST_TCP_HDR) / 4) + (UCHAR)(usOptionsBytes / 4); //* TCP头部字段实际长度（单位：32位整型）
    stHdr.usFlag = uniFlag.usVal;
    stHdr.usWinSize = htons(pstLink->stLocal.usWndSize/* - sizeof(ST_TCP_HDR) - TCP_OPTIONS_SIZE_MAX*/);
    stHdr.usChecksum = 0;
    stHdr.usUrgentPointer = 0; 
    //* 挂载到链表头部
    SHORT sHdrNode;
    sHdrNode = buf_list_get_ext((UCHAR *)&stHdr, (UINT)sizeof(ST_TCP_HDR), penErr);
    if (sHdrNode < 0)
    {
        if (sDataNode >= 0)
            buf_list_free(sDataNode);
        if (sOptionsNode >= 0)
            buf_list_free(sOptionsNode);

        onps_input_unlock(pstLink->stcbWaitAck.nInput);

        return -1;
    }
    buf_list_put_head(&sBufListHead, sHdrNode); 

    //* 填充用于校验和计算的tcp伪报头
    ST_TCP_PSEUDOHDR stPseudoHdr; 
    stPseudoHdr.unSrcAddr = unSrcAddr;
    stPseudoHdr.unDstAddr = htonl(unDstAddr);
    stPseudoHdr.ubMustBeZero = 0; 
    stPseudoHdr.ubProto = IPPROTO_TCP; 
    stPseudoHdr.usPacketLen = htons(sizeof(ST_TCP_HDR) + usOptionsBytes + usDataBytes); 
    //* 挂载到链表头部
    SHORT sPseudoHdrNode;
    sPseudoHdrNode = buf_list_get_ext((UCHAR *)&stPseudoHdr, (UINT)sizeof(ST_TCP_PSEUDOHDR), penErr);
    if (sPseudoHdrNode < 0)
    {
        if (sDataNode >= 0)
            buf_list_free(sDataNode);
        if (sOptionsNode >= 0)
            buf_list_free(sOptionsNode);
        buf_list_free(sHdrNode);

        onps_input_unlock(pstLink->stcbWaitAck.nInput);

        return -1;
    }
    buf_list_put_head(&sBufListHead, sPseudoHdrNode);

    //* 计算校验和
    stHdr.usChecksum = tcpip_checksum_ext(sBufListHead); 
    //* 用不到了，释放伪报头
    buf_list_free_head(&sBufListHead, sPseudoHdrNode);

    //* 发送之
    INT nRtnVal = ip_send_ext(unSrcAddr, unDstAddr, TCP, IP_TTL_DEFAULT, sBufListHead, penErr);
#if SUPPORT_SACK
    pstLink->stLocal.unHasSndBytes += (UINT)usDataBytes;
#endif
    onps_input_unlock(pstLink->stcbWaitAck.nInput);

    //* 释放刚才申请的buf list节点
    if (sDataNode >= 0)
        buf_list_free(sDataNode);
    if (sOptionsNode >= 0)
        buf_list_free(sOptionsNode);
    buf_list_free(sHdrNode);

    return nRtnVal; 
}

static void tcp_send_ack_of_syn_ack(INT nInput, PST_TCPLINK pstLink, in_addr_t unNetifIp, USHORT usSrcPort, UINT unSrvAckNum)
{
    //* 标志字段syn域置1，其它标志域为0
    UNI_TCP_FLAG uniFlag;
    uniFlag.usVal = 0;
    uniFlag.stb16.ack = 1;

    //* 更新tcp序号
    pstLink->stLocal.unSeqNum = unSrvAckNum; 
    pstLink->stPeer.unSeqNum += 1; 

    //* 发送
    EN_ONPSERR enErr;
    INT nRtnVal = tcp_send_packet(pstLink, unNetifIp, usSrcPort, pstLink->stPeer.stAddr.unIp, pstLink->stPeer.stAddr.usPort, 
                                    uniFlag, NULL, 0, NULL, 0, 
#if SUPPORT_SACK
        TRUE, pstLink->stPeer.unSeqNum,
#endif
        &enErr);
    if (nRtnVal > 0)
    {                  
        //* 连接成功
        pstLink->bState = (CHAR)TLSCONNECTED; 
    }
    else 
    {
        pstLink->bState = (CHAR)TLSSYNACKACKSENTFAILED;

        if (nRtnVal < 0)
            onps_set_last_error(nInput, enErr);
        else
            onps_set_last_error(nInput, ERRSENDZEROBYTES);
    }    

	if (pstLink->stcbWaitAck.bRcvTimeout)			
		onps_input_sem_post(pstLink->stcbWaitAck.nInput);	
}

INT tcp_send_syn(INT nInput, in_addr_t unSrvAddr, USHORT usSrvPort, int nConnTimeout)
{
    EN_ONPSERR enErr;    

    //* 获取链路信息存储节点
    PST_TCPLINK pstLink; 
    if (!onps_input_get(nInput, IOPT_GETTCPUDPLINK, &pstLink, &enErr))
    {
        onps_set_last_error(nInput, enErr); 
        return -1; 
    }

    //* 获取tcp链路句柄访问地址，该地址保存当前tcp链路由协议栈自动分配的端口及本地网络接口地址
    PST_TCPUDP_HANDLE pstHandle; 
    if (!onps_input_get(nInput, IOPT_GETTCPUDPADDR, &pstHandle, &enErr))
    {
        onps_set_last_error(nInput, enErr);
        return -1;
    }

    //* 先寻址，因为tcp校验和计算需要用到本地地址，同时当前tcp链路句柄也需要用此标识
    UINT unNetifIp = route_get_netif_ip(unSrvAddr);
    if (!unNetifIp)
    {
        onps_set_last_error(nInput, ERRADDRESSING);
        return -1;
    }
    //* 更新当前input句柄，以便收到应答报文时能够准确找到该链路
    pstHandle->unNetifIp = unNetifIp;
    pstHandle->usPort = onps_input_port_new(IPPROTO_TCP); 

    //* 标志字段syn域置1，其它标志域为0
    UNI_TCP_FLAG uniFlag; 
    uniFlag.usVal = 0; 
    uniFlag.stb16.syn = 1;        

    //* 填充tcp头部选项数据
    UCHAR ubaOptions[TCP_OPTIONS_SIZE_MAX]; 
    INT nOptionsSize = tcp_options_attach(ubaOptions, sizeof(ubaOptions));    

    //* 加入定时器队列
    pstLink->stcbWaitAck.bRcvTimeout = nConnTimeout; 
    pstLink->stcbWaitAck.bIsAcked = FALSE;
    pstLink->stcbWaitAck.pstTimer = one_shot_timer_new(tcp_ack_timeout_handler, pstLink, nConnTimeout ? nConnTimeout : TCP_CONN_TIMEOUT);
    if (!pstLink->stcbWaitAck.pstTimer)
    {
        onps_set_last_error(nInput, ERRNOIDLETIMER);
        return -1;
    }

    //* 完成实际的发送
    pstLink->stLocal.unSeqNum = 0; 
    pstLink->bState = TLSSYNSENT;
    INT nRtnVal = tcp_send_packet(pstLink, pstHandle->unNetifIp, pstHandle->usPort, unSrvAddr, usSrvPort, uniFlag, ubaOptions, (USHORT)nOptionsSize, NULL, 0, 
#if SUPPORT_SACK
        TRUE, 0,
#endif        
        &enErr); 
    if (nRtnVal > 0)
    {
        //* 加入定时器队列
        /*
        pstLink->stcbWaitAck.bIsAcked = FALSE; 
        pstLink->stcbWaitAck.pstTimer = one_shot_timer_new(tcp_ack_timeout_handler, pstLink, nConnTimeout ? nConnTimeout : TCP_CONN_TIMEOUT); 
        if (!pstLink->stcbWaitAck.pstTimer)
        {
            onps_set_last_error(nInput, ERRNOIDLETIMER);
            return -1;             
        }        

        pstLink->bState = TLSSYNSENT; // 只有定时器申请成功了才会将链路状态迁移到syn报文已发送状态，以确保收到syn ack时能够进行正确匹配        
        */
    }
    else
    {
        pstLink->bState = TLSINIT; 
        one_shot_timer_free(pstLink->stcbWaitAck.pstTimer);

        if(nRtnVal < 0)
            onps_set_last_error(nInput, enErr);
        else
            onps_set_last_error(nInput, ERRSENDZEROBYTES);
    }

    return nRtnVal; 
}

INT tcp_send_data(INT nInput, UCHAR *pubData, INT nDataLen, int nWaitAckTimeout)
{
    EN_ONPSERR enErr;

    //* 获取链路信息存储节点
    PST_TCPLINK pstLink;
    if (!onps_input_get(nInput, IOPT_GETTCPUDPLINK, &pstLink, &enErr))
    {
        onps_set_last_error(nInput, enErr);
        return -1;
    }

    //* 首先看看对端的mss能够接收多少数据
    INT nSndDataLen = nDataLen < (INT)pstLink->stPeer.usMSS ? nDataLen : (INT)pstLink->stPeer.usMSS; 
    //* 再看看对端的接收窗口是否足够大
    INT nWndSize = ((INT)pstLink->stPeer.usWndSize) * (INT)pow(2, pstLink->stPeer.bWndScale); 
    nSndDataLen = nSndDataLen < nWndSize ? nSndDataLen : nWndSize; 
    
    //* 标志字段push、ack域置1，其它标志域为0
    UNI_TCP_FLAG uniFlag;
    uniFlag.usVal = 0;
    uniFlag.stb16.ack = 1;
    uniFlag.stb16.push = 1;

	//* 提前赋值，因为存在发送即接收到的情况（常见于本地以太网）
    pstLink->stcbWaitAck.bRcvTimeout = nWaitAckTimeout; //* 必须更新这个值，因为send_nb()函数不等待semaphore信号，所以需要显式地告知接收逻辑收到数据发送ack时不再投递semaphore

#if !SUPPORT_SACK
	pstLink->stcbWaitAck.bIsAcked = FALSE;                      
	pstLink->stcbWaitAck.pstTimer = one_shot_timer_new(tcp_ack_timeout_handler, pstLink, nWaitAckTimeout ? nWaitAckTimeout : TCP_ACK_TIMEOUT);
	if (!pstLink->stcbWaitAck.pstTimer)
	{
		onps_set_last_error(nInput, ERRNOIDLETIMER);
		return -1;
	}    
#endif

    pstLink->stPeer.bIsNotAcked = FALSE; 

    pstLink->stcbWaitAck.usSendDataBytes = (USHORT)nSndDataLen; //* 记录当前实际发送的字节数
    pstLink->stLocal.bDataSendState = TDSSENDING;
    INT nRtnVal = tcp_send_packet(pstLink, pstLink->stLocal.pstAddr->unNetifIp, pstLink->stLocal.pstAddr->usPort, pstLink->stPeer.stAddr.unIp, 
                                    pstLink->stPeer.stAddr.usPort, uniFlag, NULL, 0, pubData, (USHORT)nSndDataLen, 
#if SUPPORT_SACK
        FALSE, 0,
#endif
        &enErr); 
    if (nRtnVal > 0)
    {        
        //* 加入定时器队列
        //pstLink->stcbWaitAck.bIsAcked = FALSE;
        //pstLink->stcbWaitAck.pstTimer = one_shot_timer_new(tcp_ack_timeout_handler, pstLink, nWaitAckTimeout ? nWaitAckTimeout : TCP_ACK_TIMEOUT);
        //if (!pstLink->stcbWaitAck.pstTimer)
        //{
        //    onps_set_last_error(nInput, ERRNOIDLETIMER);
        //    return -1;
        //}
        
        //* 记录当前实际发送的字节数
        //pstLink->stcbWaitAck.usSendDataBytes = (USHORT)nSndDataLen; 
        //pstLink->stLocal.bDataSendState = TDSSENDING; 
        return nSndDataLen; 
    }
    else
    {
        pstLink->stLocal.bDataSendState = TDSSENDRDY;

    #if !SUPPORT_SACK
		one_shot_timer_free(pstLink->stcbWaitAck.pstTimer);
    #endif

        if (nRtnVal < 0)
            onps_set_last_error(nInput, enErr);
        else
            onps_set_last_error(nInput, ERRSENDZEROBYTES);
    }

    return nRtnVal;
}

#if SUPPORT_SACK
INT tcp_send_data_ext(INT nInput, UCHAR *pubData, INT nDataLen, UINT unSeqNum)
{
    EN_ONPSERR enErr;

    //* 获取链路信息存储节点
    PST_TCPLINK pstLink;
    if (!onps_input_get(nInput, IOPT_GETTCPUDPLINK, &pstLink, &enErr))
    {
        onps_set_last_error(nInput, enErr);
        return -1;
    }

    //* 首先看看对端的mss能够接收多少数据
    INT nSndDataLen = nDataLen < (INT)pstLink->stPeer.usMSS ? nDataLen : (INT)pstLink->stPeer.usMSS;
    //* 再看看对端的接收窗口是否足够大
    INT nWndSize = ((INT)pstLink->stPeer.usWndSize) * (INT)pow(2, pstLink->stPeer.bWndScale);
    nSndDataLen = nSndDataLen < nWndSize ? nSndDataLen : nWndSize;

    //* 标志字段push、ack域置1，其它标志域为0
    UNI_TCP_FLAG uniFlag;
    uniFlag.usVal = 0;
    uniFlag.stb16.ack = 1;
    uniFlag.stb16.push = 1;

    //* 提前赋值，因为存在发送即接收到的情况（常见于本地以太网）
    pstLink->stcbWaitAck.bRcvTimeout = 0; //* 必须更新这个值，因为send_nb()函数不等待semaphore信号，所以需要显式地告知接收逻辑收到数据发送ack时不再投递semaphore

    pstLink->stPeer.bIsNotAcked = FALSE;

    pstLink->stcbWaitAck.usSendDataBytes = (USHORT)nSndDataLen; //* 记录当前实际发送的字节数
    pstLink->stLocal.bDataSendState = TDSSENDING;
    INT nRtnVal = tcp_send_packet(pstLink, pstLink->stLocal.pstAddr->unNetifIp, pstLink->stLocal.pstAddr->usPort, pstLink->stPeer.stAddr.unIp, 
                                    pstLink->stPeer.stAddr.usPort, uniFlag, NULL, 0, pubData, (USHORT)nSndDataLen, TRUE, unSeqNum, &enErr);
    if (nRtnVal > 0)
        return nSndDataLen;    
    else
    {
        pstLink->stLocal.bDataSendState = TDSSENDRDY;

        if (nRtnVal < 0)
            onps_set_last_error(nInput, enErr);
        else
            onps_set_last_error(nInput, ERRSENDZEROBYTES);
    }

    return nRtnVal;
}
#endif

static void tcp_send_fin(PST_TCPLINK pstLink)
{
    //* 标志字段fin、ack域置1，其它标志域为0
    UNI_TCP_FLAG uniFlag;
    uniFlag.usVal = 0;
    uniFlag.stb16.ack = 1; 
    uniFlag.stb16.fin = 1;    

    //* 发送链路结束报文
    tcp_send_packet(pstLink, pstLink->stLocal.pstAddr->unNetifIp, pstLink->stLocal.pstAddr->usPort, pstLink->stPeer.stAddr.unIp, pstLink->stPeer.stAddr.usPort, uniFlag, NULL, 0, NULL, 0, 
#if SUPPORT_SACK
        FALSE, 0,
#endif
        NULL);     
}

void tcp_disconnect(INT nInput)
{
    EN_ONPSERR enErr;

    //* 获取链路信息存储节点
    PST_TCPLINK pstLink;
    if (!onps_input_get(nInput, IOPT_GETTCPUDPLINK, &pstLink, &enErr))
    {
        onps_set_last_error(nInput, enErr);
        return;
    }

    //* 未连接的话直接返回，没必要发送结束连接报文
    if (TLSCONNECTED != (EN_TCPLINKSTATE)pstLink->bState)
        return;

    //* 一旦进入fin操作，bIsAcked不再被使用，为了节约内存，这里用于close操作计时
    pstLink->stcbWaitAck.bIsAcked = 0;
    pstLink->bIsPassiveFin = FALSE;

    //* 只有状态迁移成功才会发送fin报文
    if (onps_input_set_tcp_close_state(nInput, TLSFINWAIT1))
    {        
        tcp_send_fin(pstLink);

        //* 加入定时器队列          
        pstLink->stcbWaitAck.pstTimer = one_shot_timer_new(tcp_close_timeout_handler, pstLink, 1);
        if (!pstLink->stcbWaitAck.pstTimer)
            onps_set_last_error(pstLink->stcbWaitAck.nInput, ERRNOIDLETIMER);
    }
}

void tcp_send_ack(PST_TCPLINK pstLink, in_addr_t unSrcAddr, USHORT usSrcPort, in_addr_t unDstAddr, USHORT usDstPort)
{
    //* 标志字段ack域置1，其它标志域为0
    UNI_TCP_FLAG uniFlag;
    uniFlag.usVal = 0;
    uniFlag.stb16.ack = 1;
    pstLink->stPeer.bIsNotAcked = FALSE;

    //* 发送应答报文 
    tcp_send_packet(pstLink, unSrcAddr, usSrcPort, unDstAddr, usDstPort, uniFlag, NULL, 0, NULL, 0, 
#if SUPPORT_SACK
        FALSE, 0,
#endif
        NULL);    
}

static INT tcpsrv_send_syn_ack(PST_TCPLINK pstLink, in_addr_t unSrcAddr, USHORT usSrcPort, in_addr_t unDstAddr, USHORT usDstPort, EN_ONPSERR *penErr)
{
    //* 标志字段ack域置1，其它标志域为0
    UNI_TCP_FLAG uniFlag;
    uniFlag.usVal = 0;
    uniFlag.stb16.syn = 1; 
    uniFlag.stb16.ack = 1;

    //* 填充tcp头部选项数据
    UCHAR ubaOptions[TCP_OPTIONS_SIZE_MAX];
    INT nOptionsSize = tcp_options_attach(ubaOptions, sizeof(ubaOptions));

    //* 完成实际的发送
    return tcp_send_packet(pstLink, unSrcAddr, usSrcPort, unDstAddr, usDstPort, uniFlag, ubaOptions, (USHORT)nOptionsSize, NULL, 0, 
#if SUPPORT_SACK
        FALSE, 0,
#endif
        penErr); 
}

static void tcpsrv_send_syn_ack_with_start_timer(PST_TCPLINK pstLink, in_addr_t unSrcAddr, USHORT usSrcPort, in_addr_t unDstAddr, USHORT usDstPort)
{
    EN_ONPSERR enErr; 

    //* 启动一个定时器等待对端的ack到达，只有收到ack才能真正建立tcp双向链路，否则只能算是半开链路（或称作半连接链路，客户端只能发送无法接收）            
    pstLink->stcbWaitAck.pstTimer = one_shot_timer_new(tcpsrv_syn_recv_timeout_handler, pstLink, pstLink->stcbWaitAck.bRcvTimeout);
    if (pstLink->stcbWaitAck.pstTimer)
    {
        //* 发送syn ack报文给对端
        pstLink->bState = TLSSYNACKSENT;         
        if (tcpsrv_send_syn_ack(pstLink, unSrcAddr, usSrcPort, unDstAddr, usDstPort, &enErr) < 0)
        {
            pstLink->bState = TLSRCVEDSYN;
            one_shot_timer_free(pstLink->stcbWaitAck.pstTimer);
            onps_input_free(pstLink->stcbWaitAck.nInput); 

    #if SUPPORT_PRINTF && DEBUG_LEVEL > 1
            CHAR szAddr[20], szAddrClt[20];
        #if PRINTF_THREAD_MUTEX
            os_thread_mutex_lock(o_hMtxPrintf);
        #endif
            printf("tcpsrv_send_syn_ack() failed (server %s:%d, client: %s:%d), %s\r\n", inet_ntoa_safe_ext(unSrcAddr, szAddr), usSrcPort, inet_ntoa_safe_ext(unDstAddr, szAddrClt), usDstPort, onps_error(enErr));
        #if PRINTF_THREAD_MUTEX
            os_thread_mutex_unlock(o_hMtxPrintf);
        #endif
    #endif
        }        
    }
    else
    {
        onps_input_free(pstLink->stcbWaitAck.nInput); 

#if SUPPORT_PRINTF            
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_lock(o_hMtxPrintf);
    #endif
        printf("%s\r\n", onps_error(ERRNOIDLETIMER));
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_unlock(o_hMtxPrintf);
    #endif
#endif
    }
}

#if SUPPORT_SACK
static void tcp_link_fast_retransmit(PST_TCPLINK pstLink, UINT unRetransSeqNum)
{
    tcp_send_timer_lock();
    {
        PSTCB_TCPSENDTIMER pstSendTimer = pstLink->stcbSend.pstcbSndTimer;
        while (pstSendTimer)
        {
            if (pstSendTimer->unLeft == unRetransSeqNum - 1)
            {
                //* 取出数据重新发送
                EN_ONPSERR enErr;
                UCHAR *pubData = (UCHAR *)buddy_alloc(pstSendTimer->unRight - pstSendTimer->unLeft, &enErr);
                if (pubData)
                {
                    UINT unStartReadIdx = pstSendTimer->unLeft % TCPSNDBUF_SIZE;
                    UINT unEndReadIdx = pstSendTimer->unRight % TCPSNDBUF_SIZE;
                    if (unEndReadIdx > unStartReadIdx)
                        memcpy(pubData, pstLink->stcbSend.pubSndBuf + unStartReadIdx, unEndReadIdx - unStartReadIdx);
                    else
                    {
                        UINT unCpyBytes = TCPSNDBUF_SIZE - unStartReadIdx;
                        memcpy(pubData, pstLink->stcbSend.pubSndBuf + unStartReadIdx, unCpyBytes);
                        memcpy(pubData + unCpyBytes, pstLink->stcbSend.pubSndBuf, unEndReadIdx);
                    }

                    //* 重发dup ack的数据块
                    tcp_send_data_ext(pstLink->stcbWaitAck.nInput, pubData, pstSendTimer->unRight - pstSendTimer->unLeft, pstSendTimer->unLeft + 1);
                    buddy_free(pubData);

                    //* 将timer从当前位置转移到队列的尾部，并重新开启重传计时
                    pstSendTimer->unSendMSecs = os_get_system_msecs();
                    tcp_send_timer_node_del_unsafe(pstSendTimer);
                    tcp_send_timer_node_put_unsafe(pstSendTimer);

                    //os_critical_init();
                    //os_enter_critical();
                    //pstLink->stcbSend.unRetransSeqNum = 0;
                    //os_exit_critical();
                }
                else
                {
            #if SUPPORT_PRINTF && DEBUG_LEVEL
                #if PRINTF_THREAD_MUTEX
                    os_thread_mutex_lock(o_hMtxPrintf);
                #endif
                    printf("tcp_link_fast_retransmit() failed, %s\r\n", onps_error(enErr));
                #if PRINTF_THREAD_MUTEX
                    os_thread_mutex_unlock(o_hMtxPrintf);
                #endif
            #endif
                }

                break;
            }

            pstSendTimer = pstSendTimer->pstcbNext;
        }
    }
    tcp_send_timer_unlock();
}

static void tcp_link_sack_handler(PST_TCPLINK pstLink, CHAR bSackNum)
{    
    UINT unStartSeq = pstLink->stLocal.unSeqNum;

    //* 确定是否为d-sack，判断依据如下：
    //* 1) 第一个不连续块被ack序号覆盖；
    //* 2）第一个不连续块被第二个不连续块覆盖；
    //* 看看是否被ack范围覆盖，如果覆盖则说明这是一个d-sack，从第二段开始重发数据
    CHAR i = 0;
    if (uint_before(pstLink->stcbSend.staSack[0].unLeft, unStartSeq))
        i = 1; 
    else if (bSackNum > 1)
    {
        if (!uint_after(pstLink->stcbSend.staSack[0].unRight, pstLink->stcbSend.staSack[1].unRight)
            && !uint_before(pstLink->stcbSend.staSack[0].unLeft, pstLink->stcbSend.staSack[1].unLeft))
            i = 1; 
        else; 
    }
    else; 
    
    //* 发送数据    
    for (; i < bSackNum; i++)
    {        
        //* 先判断sack范围是否合理
        if (uint_after(pstLink->stcbSend.staSack[i].unLeft, unStartSeq) && uint_before(pstLink->stcbSend.staSack[i].unRight, pstLink->stcbSend.unWriteBytes))
        {
            tcp_link_fast_retransmit(pstLink, pstLink->stcbSend.staSack[i].unLeft);
            unStartSeq = pstLink->stcbSend.staSack[i].unRight;
        }
    }
}
#endif

void tcp_recv(in_addr_t unSrcAddr, in_addr_t unDstAddr, UCHAR *pubPacket, INT nPacketLen)
{
    PST_TCP_HDR pstHdr = (PST_TCP_HDR)pubPacket; 
    
    //* 把完整的tcp报文与tcp伪包头链接到一起，以便计算tcp校验和确保收到的tcp报文正确
    EN_ONPSERR enErr; 
    SHORT sBufListHead = -1;
    SHORT sTcpPacketNode = -1;
    sTcpPacketNode = buf_list_get_ext(pubPacket, nPacketLen, &enErr);
    if (sTcpPacketNode < 0)
    {
#if SUPPORT_PRINTF && DEBUG_LEVEL
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_lock(o_hMtxPrintf);
    #endif
        printf("buf_list_get_ext() failed, %s, the tcp packet will be dropped\r\n", onps_error(enErr));
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_unlock(o_hMtxPrintf);
    #endif
#endif
        return;
    }
    buf_list_put_head(&sBufListHead, sTcpPacketNode);

    //* 填充用于校验和计算的tcp伪报头
    ST_TCP_PSEUDOHDR stPseudoHdr;
    stPseudoHdr.unSrcAddr = unSrcAddr;
    stPseudoHdr.unDstAddr = unDstAddr;
    stPseudoHdr.ubMustBeZero = 0;
    stPseudoHdr.ubProto = IPPROTO_TCP;
    stPseudoHdr.usPacketLen = htons((USHORT)nPacketLen); 
    //* 挂载到链表头部
    SHORT sPseudoHdrNode;
    sPseudoHdrNode = buf_list_get_ext((UCHAR *)&stPseudoHdr, (USHORT)sizeof(ST_TCP_PSEUDOHDR), &enErr);
    if (sPseudoHdrNode < 0)
    {        
#if SUPPORT_PRINTF && DEBUG_LEVEL
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_lock(o_hMtxPrintf);
    #endif
        printf("buf_list_get_ext() failed, %s, the tcp packet will be dropped\r\n", onps_error(enErr));
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_unlock(o_hMtxPrintf);
    #endif
#endif

        buf_list_free(sTcpPacketNode); 
        return;
    }
    buf_list_put_head(&sBufListHead, sPseudoHdrNode);

    //* 挂载完毕，可以计算校验和是否正确了
    USHORT usPktChecksum = pstHdr->usChecksum;
    pstHdr->usChecksum = 0;
    USHORT usChecksum = tcpip_checksum_ext(sBufListHead);
    //* 先释放再判断
    buf_list_free(sTcpPacketNode);
    buf_list_free(sPseudoHdrNode);
    if (usPktChecksum != usChecksum)
    {
#if SUPPORT_PRINTF && DEBUG_LEVEL > 3
        pstHdr->usChecksum = usPktChecksum;
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_lock(o_hMtxPrintf);
    #endif
        printf("checksum error (%04X, %04X), the tcp packet will be dropped\r\n", usChecksum, usPktChecksum);
        printf_hex(pubPacket, nPacketLen, 48);
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_unlock(o_hMtxPrintf);
    #endif
#endif
        return;
    }

    //* 先查找当前链路是否存在
    USHORT usDstPort = htons(pstHdr->usDstPort);
    UINT unCltIp = htonl(unSrcAddr);
    USHORT usCltPort = htons(pstHdr->usSrcPort);
    PST_TCPLINK pstLink;     
    INT nInput = onps_input_get_handle(IPPROTO_TCP, unDstAddr, usDstPort, &pstLink);
    if (nInput < 0)
    {
#if SUPPORT_PRINTF && DEBUG_LEVEL > 3
        UCHAR *pubAddr = (UCHAR *)&unDstAddr;
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_lock(o_hMtxPrintf);
    #endif        
        printf("The tcp link of %d.%d.%d.%d:%d isn't found, the packet will be dropped\r\n", pubAddr[0], pubAddr[1], pubAddr[2], pubAddr[3], usDstPort);        
    #if PRINTF_THREAD_MUTEX
        os_thread_mutex_unlock(o_hMtxPrintf);
    #endif
#endif
        return; 
    }

    //* 依据报文头部标志字段确定下一步的处理逻辑        
    UINT unSrcAckNum = htonl(pstHdr->unAckNum);
    UINT unPeerSeqNum = htonl(pstHdr->unSeqNum); 
    UNI_TCP_FLAG uniFlag; 
    uniFlag.usVal = pstHdr->usFlag;
	INT nTcpHdrLen = uniFlag.stb16.hdr_len * 4;
    if (uniFlag.stb16.ack)
    {         
    #if SUPPORT_ETHERNET
        //* 如果为NULL则说明是tcp服务器，需要再次遍历input链表找出其先前分配的链路信息及input句柄
        if(!pstLink) 
        { 
            INT nRmtCltInput = onps_input_get_handle_of_tcp_rclient(unDstAddr, usDstPort, unCltIp, usCltPort, &pstLink); 
            if (nRmtCltInput < 0) //* 尚未收到任何syn连接请求报文，这里就直接丢弃该报文
                return; 

            //* 说明尚未收到过ACK报文，这里就需要先等待这个报文
            if (TLSSYNACKSENT == pstLink->bState)
            {
                //* 序号完全相等才意味着这是一个合法的syn ack's ack报文
                if (unSrcAckNum == pstLink->stLocal.unSeqNum + 1 && unPeerSeqNum == pstLink->stPeer.unSeqNum)
                {                                        
                    //* 投递信号给accept()函数
                    onps_input_sem_post_tcpsrv_accept(nInput, nRmtCltInput, unSrcAckNum); 
                }
            }

            nInput = nRmtCltInput; 
        }
    #endif

        //* 连接请求的应答报文
        if (uniFlag.stb16.syn)
        {                                    
            if (TLSSYNSENT == pstLink->bState && unSrcAckNum == pstLink->stLocal.unSeqNum + 1) //* 确定这是一个有效的syn ack报文才可进入下一个处理流程，否则报文将被直接抛弃
            {
                pstLink->stcbWaitAck.bIsAcked = TRUE; 
                one_shot_timer_safe_free(pstLink->stcbWaitAck.pstTimer);                                 
                //one_shot_timer_recount(pstLink->stcbWaitAck.pstTimer, 1); //* 通知定时器结束计时，释放占用的非常宝贵的定时器资源

                //* 记录当前链路信息
                pstLink->stPeer.unSeqNum = unPeerSeqNum;
                pstLink->stPeer.usWndSize = htons(pstHdr->usWinSize); 
                pstLink->stPeer.stAddr.unIp = unCltIp/*htonl(unSrcAddr)*/;
                pstLink->stPeer.stAddr.usPort = usCltPort/*htons(pstHdr->usSrcPort)*/;

                //* 截取tcp头部选项字段
                tcp_options_get(pstLink, pubPacket + sizeof(ST_TCP_HDR), nTcpHdrLen - (INT)sizeof(ST_TCP_HDR));

                //* 状态迁移到已接收到syn ack报文
                pstLink->bState = TLSRCVEDSYNACK;

                //* 发送syn ack的ack报文
                tcp_send_ack_of_syn_ack(nInput, pstLink, unDstAddr, usDstPort, unSrcAckNum);
            }
        }
        else if (uniFlag.stb16.reset)
        {   
            if (pstLink->bState < TLSFINWAIT1)
            {
                //* 状态迁移到链路已被对端复位的状态
                pstLink->bState = TLSRESET;
            }
            if (pstLink->stLocal.bDataSendState == TDSSENDING)
                pstLink->stLocal.bDataSendState = TDSLINKRESET; 

            //if (INVALID_HSEM != pstLink->stcbWaitAck.hSem)
            //    os_thread_sem_post(pstLink->stcbWaitAck.hSem); 			
            onps_input_sem_post(nInput);
        }
        else if (uniFlag.stb16.fin)
        {                       
            if (pstLink->stLocal.bDataSendState == TDSSENDING)
            {
                pstLink->stLocal.bDataSendState = TDSLINKCLOSED;                                 

				if (pstLink->stcbWaitAck.bRcvTimeout)									
					onps_input_sem_post(pstLink->stcbWaitAck.nInput);
            }            

            //* 发送ack
            pstLink->stPeer.unSeqNum = unPeerSeqNum + 1;
            tcp_send_ack(pstLink, unDstAddr, usDstPort, unCltIp/*htonl(unSrcAddr)*/, usCltPort/*htons(pstHdr->usSrcPort)*/);
            //* 迁移到相关状态
            if (TLSCONNECTED == (EN_TCPLINKSTATE)pstLink->bState)
            {                       
                if (onps_input_set_tcp_close_state(nInput, TLSFINWAIT1))
                {
                    onps_input_set_tcp_close_state(nInput, TLSFINWAIT2); //* 进入FIN_WAIT2态
                    pstLink->bIsPassiveFin = TRUE;

                    //* 同样立即下发结束报文，本地也不再继续发送了（而不是按照协议约定允许上层用户继续在半关闭状态下发送数据）
                    tcp_send_fin(pstLink);                   

                    //* 加入定时器队列                      
                    pstLink->stcbWaitAck.pstTimer = one_shot_timer_new(tcp_close_timeout_handler, pstLink, 1);                    

                    if (!pstLink->stcbWaitAck.pstTimer)
                        onps_set_last_error(pstLink->stcbWaitAck.nInput, ERRNOIDLETIMER);
                    onps_input_set_tcp_close_state(nInput, TLSTIMEWAIT); //* 等待对端的ACK，然后结束
                }                                
            }
            else if(TLSFINWAIT1 == (EN_TCPLINKSTATE)pstLink->bState)
                onps_input_set_tcp_close_state(nInput, TLSCLOSING);
            else if(TLSFINWAIT2 == (EN_TCPLINKSTATE)pstLink->bState)
                onps_input_set_tcp_close_state(nInput, TLSTIMEWAIT); 
            else; 
        }
        else
        {               
            //* 看看有数据吗？            
            INT nDataLen = nPacketLen - nTcpHdrLen;

            //* 处于关闭状态
            if ((EN_TCPLINKSTATE)pstLink->bState > TLSRESET && (EN_TCPLINKSTATE)pstLink->bState < TLSCLOSED)
            {
                if(TLSFINWAIT1 == (EN_TCPLINKSTATE)pstLink->bState)
                    onps_input_set_tcp_close_state(nInput, TLSFINWAIT2); 
                else if(TLSCLOSING == (EN_TCPLINKSTATE)pstLink->bState)                       
                    onps_input_set_tcp_close_state(nInput, TLSTIMEWAIT);
                else if(TLSTIMEWAIT == (EN_TCPLINKSTATE)pstLink->bState)
                {
                    if(pstLink->bIsPassiveFin)
                        onps_input_set_tcp_close_state(nInput, TLSCLOSED); 
                }
                else;                 

                if (nDataLen)
                    onps_input_recv(nInput, NULL, 0, 0, 0, NULL);                 
            }            

        #if SUPPORT_SACK
            if (unSrcAckNum == pstLink->stcbSend.unPrevSeqNum) //* 记录dup ack数量
            {
                pstLink->stcbSend.bDupAckNum++;                
                if (pstLink->stcbSend.bDupAckNum > 3)
                {
                    //* 看看是否存在sack选项
                    if (nTcpHdrLen > sizeof(ST_TCP_HDR))
                    {
                        CHAR bSackNum = tcp_options_get_sack(pstLink, pubPacket + sizeof(ST_TCP_HDR), nTcpHdrLen - (INT)sizeof(ST_TCP_HDR));
                        if (bSackNum)
                            tcp_link_sack_handler(pstLink, bSackNum);
                        else
                            tcp_link_fast_retransmit(pstLink, unSrcAckNum);
                    }
                    else
                        tcp_link_fast_retransmit(pstLink, unSrcAckNum); 

                    pstLink->stcbSend.bDupAckNum = 0; 
                }                
            }
            else
                pstLink->stcbSend.bDupAckNum = 1;

            //* 看看是否存在sack选项
            /*
            if (nTcpHdrLen > sizeof(ST_TCP_HDR))
            {
                CHAR bSackNum = tcp_options_get_sack(pstLink, pubPacket + sizeof(ST_TCP_HDR), nTcpHdrLen - (INT)sizeof(ST_TCP_HDR));
                if(bSackNum)
                    tcp_link_sack_handler(pstLink, bSackNum);
            }
            else
            {
                // 如果不存在sack选项，看看是否出现dup ack情形，存在则立即重发丢失的数据
                if (pstLink->stcbSend.bDupAckNum > 3)
                {
                    tcp_link_fast_retransmit(pstLink, unSrcAckNum); 
                    pstLink->stcbSend.bDupAckNum = 0;
                }
            }
            */

            //* 收到应答，更新当前数据发送序号
            pstLink->stcbSend.unPrevSeqNum = unSrcAckNum;
            pstLink->stLocal.unSeqNum = unSrcAckNum; 

            tcp_link_for_send_data_put(pstLink);
            tcp_send_sem_post();
        #else
            //* 已经发送了数据，看看是不是对应的ack报文            			
            if (TDSSENDING == (EN_TCPDATASNDSTATE)pstLink->stLocal.bDataSendState && unSrcAckNum == pstLink->stLocal.unSeqNum + (UINT)pstLink->stcbWaitAck.usSendDataBytes)
            {                
                //* 收到应答，更新当前数据发送序号            
                pstLink->stLocal.unSeqNum = unSrcAckNum;				

                pstLink->stcbWaitAck.bIsAcked = TRUE; 				
                one_shot_timer_safe_free(pstLink->stcbWaitAck.pstTimer);                 				

                //* 数据发送状态迁移至已收到ACK报文状态，并通知发送者当前数据已发送成功
                pstLink->stLocal.bDataSendState = (CHAR)TDSACKRCVED;                 
				if (pstLink->stcbWaitAck.bRcvTimeout)									
					onps_input_sem_post(pstLink->stcbWaitAck.nInput);
            }
			/*
			else
			{
				os_thread_mutex_lock(o_hMtxPrintf);
				printf("<%d> %08X %d %d %d data: %d\r\n", pstLink->stLocal.bDataSendState, unPeerSeqNum, unSrcAckNum, pstLink->stLocal.unSeqNum, (UINT)pstLink->stcbWaitAck.usSendDataBytes, nDataLen);
				os_thread_mutex_unlock(o_hMtxPrintf);
			}
			*/
        #endif
            
            //* 有数据则处理之
            if (nDataLen)
            {                          
                //* 只有序号不同才会搬运数据，确认过序号的说明已经搬运不能重复搬运了，对端重复发送的原因是没有收到ack报文，所以只需再次发送ack报文即可
                if (unPeerSeqNum != pstLink->stLocal.unAckNum && unPeerSeqNum > pstLink->stLocal.unAckNum)
                {
                    //* 必须是非零窗口探测报文才搬运数据
                    if (!(nDataLen == 1 && pstLink->stLocal.bIsZeroWnd))
                    {
                        //* 将数据搬运到input层
                        if (onps_input_recv(nInput, (const UCHAR *)(pubPacket + nTcpHdrLen), nDataLen, 0, 0, &enErr))
                            pstLink->stLocal.unAckNum = unPeerSeqNum;
                        else
                        {
                    #if SUPPORT_PRINTF && DEBUG_LEVEL
                        #if PRINTF_THREAD_MUTEX
                            os_thread_mutex_lock(o_hMtxPrintf);
                        #endif
                            printf("onps_input_recv() failed, %s, the tcp packet will be dropped\r\n", onps_error(enErr));
                        #if PRINTF_THREAD_MUTEX
                            os_thread_mutex_unlock(o_hMtxPrintf);
                        #endif
                    #endif
                            return;
                        }
                    }                    
                }

                //* 更新对端的窗口信息                                                    
                pstLink->stPeer.usWndSize = htons(pstHdr->usWinSize);

                //* 如果是零窗口探测报文则只更新当前窗口大小并通知给对端
                if (nDataLen == 1 && pstLink->stLocal.bIsZeroWnd)
                {
                    USHORT usWndSize = pstLink->stLocal.usWndSize;
                    if (usWndSize)                    
                        pstLink->stLocal.bIsZeroWnd = FALSE; 
                    pstLink->stPeer.unSeqNum = unPeerSeqNum;
                    tcp_send_ack(pstLink, unDstAddr, usDstPort, unCltIp/*htonl(unSrcAddr)*/, usCltPort/*htons(pstHdr->usSrcPort)*/);
                }
                else
                {
                    pstLink->stPeer.unSeqNum = unPeerSeqNum + nDataLen;
                    if (pstLink->uniFlags.stb16.no_delay_ack || (EN_TCPLINKSTATE)pstLink->bState != TLSCONNECTED)
                        tcp_send_ack(pstLink, unDstAddr, usDstPort, unCltIp/*htonl(unSrcAddr)*/, usCltPort/*htons(pstHdr->usSrcPort)*/);
                    else
                    {
                        pstLink->stPeer.unStartMSecs = os_get_system_msecs(); 
                        pstLink->stPeer.bIsNotAcked = TRUE;
                    }
                }
            }            
        }
    }
#if SUPPORT_ETHERNET
    //* 只有tcp syn连接请求报文才没有ack标志
    else
    {       
        //* 首先看看是否已针对当前请求申请了input
        INT nRmtCltInput = onps_input_get_handle_of_tcp_rclient(unDstAddr, usDstPort, unCltIp, usCltPort, &pstLink); 
        if (nRmtCltInput < 0) //* 尚未申请input节点，这里需要先申请一个
        {                 
            nRmtCltInput = onps_input_new_tcp_remote_client(nInput, usDstPort, unDstAddr, usCltPort, unCltIp, &pstLink, &enErr);             
            if (nRmtCltInput < 0)
            {
        #if SUPPORT_PRINTF                
            #if PRINTF_THREAD_MUTEX
                os_thread_mutex_lock(o_hMtxPrintf);
            #endif
                printf("%s\r\n", onps_error(enErr));
            #if PRINTF_THREAD_MUTEX
                os_thread_mutex_unlock(o_hMtxPrintf);
            #endif
        #endif
                return; 
            }

            //* 截取tcp头部选项字段
            tcp_options_get(pstLink, pubPacket + sizeof(ST_TCP_HDR), nTcpHdrLen - (INT)sizeof(ST_TCP_HDR));

            //* 发送syn ack报文给对端
            pstLink->stPeer.unSeqNum = unPeerSeqNum + 1;
            tcpsrv_send_syn_ack_with_start_timer(pstLink, unDstAddr, usDstPort, unCltIp, usCltPort);
        }
        else //* 已经申请，说明对端没收到syn ack，我们已经通过定时器在重复发送syn ack报文，所以这里直接发送一个应答但不开启定时器
        {
            //* 截取tcp头部选项字段
            tcp_options_get(pstLink, pubPacket + sizeof(ST_TCP_HDR), nTcpHdrLen - (INT)sizeof(ST_TCP_HDR)); 

            //* 发送syn ack报文给对端
            pstLink->stPeer.unSeqNum = unPeerSeqNum + 1;
            tcpsrv_send_syn_ack(pstLink, unDstAddr, usDstPort, unCltIp, usCltPort, &enErr);             
        }                
    }
#endif
}

INT tcp_recv_upper(INT nInput, UCHAR *pubDataBuf, UINT unDataBufSize, CHAR bRcvTimeout)
{
    EN_ONPSERR enErr;
    INT nRcvedBytes; 

    //* 读取数据
    nRcvedBytes = onps_input_recv_upper(nInput, pubDataBuf, unDataBufSize, NULL, NULL, &enErr);
	if (nRcvedBytes > 0)
	{    		
		if (bRcvTimeout > 0)
			onps_input_sem_pend(nInput, 1, NULL); //* 因为收到数据了，所以一定存在这个信号，所以这里主动消除该信号，确保用户端的延时准确
		return nRcvedBytes;
	}
    else
    {
        if(nRcvedBytes < 0)
            goto __lblErr;
    }

    //* 等待后
    if (bRcvTimeout)
    {
        CHAR bWaitSecs = bRcvTimeout;
        
__lblWaitRecv:         
        if (bRcvTimeout > 0)
        {			
            if (onps_input_sem_pend(nInput, 1, &enErr) < 0)            
                goto __lblErr;            

            bWaitSecs--; 			
        }
        else
        {
            if (onps_input_sem_pend(nInput, 0, &enErr) < 0)            
                goto __lblErr;             
        }
				
        //* 读取数据
        nRcvedBytes = onps_input_recv_upper(nInput, pubDataBuf, unDataBufSize, NULL, NULL, &enErr);		
        if (nRcvedBytes > 0)
            return nRcvedBytes;
        else
        {			
            if (nRcvedBytes < 0)
                goto __lblErr;

            if (bWaitSecs <= 0) 
                return 0; 
            goto __lblWaitRecv; 
        }
    }
    else
        return 0; 


__lblErr: 
    onps_set_last_error(nInput, enErr);
    return -1;
}

#if SUPPORT_SACK
static BOOL tcp_link_send_data(PST_TCPLINK pstLink)
{
    UINT unStartSeqNum = pstLink->stLocal.unSeqNum - 1;
    STCB_TCPSENDTIMER **ppstSendTimer = (STCB_TCPSENDTIMER **)&pstLink->stcbSend.pstcbSndTimer;
    PSTCB_TCPSENDTIMER pstSendTimer = pstLink->stcbSend.pstcbSndTimer;        
    CHAR i; 
    for (i = 0; i < pstLink->stcbSend.bSendPacketNum; i++)
    {
        unStartSeqNum = pstSendTimer->unRight;
        ppstSendTimer = (STCB_TCPSENDTIMER **)&pstSendTimer->pstcbNextForLink;
        pstSendTimer = pstSendTimer->pstcbNextForLink; 
    }
    
    //* 存在数据
    if (pstLink->stcbSend.unWriteBytes != unStartSeqNum)
    {
        *ppstSendTimer = tcp_send_timer_node_get();
        if (NULL != *ppstSendTimer)
        {
            pstSendTimer = *ppstSendTimer; 
            pstSendTimer->pstLink = pstLink; 

            //* 看看剩余数据是否超出了pstLink->stPeer.usMSS参数指定的长度
            UINT unCpyBytes = pstLink->stcbSend.unWriteBytes - unStartSeqNum; 
            if (unCpyBytes > pstLink->stPeer.usMSS)
                unCpyBytes = pstLink->stPeer.usMSS; 
            EN_ONPSERR enErr;
            UCHAR *pubData = (UCHAR *)buddy_alloc(unCpyBytes, &enErr);
            if (pubData)
            {
                pstSendTimer->unLeft = unStartSeqNum; 
                pstSendTimer->unRight = unStartSeqNum + unCpyBytes;

                UINT unStartReadIdx = pstSendTimer->unLeft % TCPSNDBUF_SIZE;
                UINT unEndReadIdx = pstSendTimer->unRight % TCPSNDBUF_SIZE;
                if (unEndReadIdx > unStartReadIdx)
                    memcpy(pubData, pstLink->stcbSend.pubSndBuf + unStartReadIdx, unEndReadIdx - unStartReadIdx);
                else
                {
                    UINT unCpyBytes = TCPSNDBUF_SIZE - unStartReadIdx;
                    memcpy(pubData, pstLink->stcbSend.pubSndBuf + unStartReadIdx, unCpyBytes);
                    memcpy(pubData + unCpyBytes, pstLink->stcbSend.pubSndBuf, unEndReadIdx);
                }
                
                //* 计时并将其放入发送定时器队列
                pstSendTimer->unSendMSecs = os_get_system_msecs();
                pstSendTimer->usRto = RTO; 
                tcp_send_timer_node_put(pstSendTimer);
                pstLink->stcbSend.bSendPacketNum++; 

                //* 发送数据                
                tcp_send_data(pstLink->stcbWaitAck.nInput, pubData, unCpyBytes, 0);
                buddy_free(pubData);                
            }
            else
            {
        #if SUPPORT_PRINTF && DEBUG_LEVEL
            #if PRINTF_THREAD_MUTEX
                os_thread_mutex_lock(o_hMtxPrintf);
            #endif
                printf("tcp_link_send_data() failed, %s\r\n", onps_error(enErr));
            #if PRINTF_THREAD_MUTEX
                os_thread_mutex_unlock(o_hMtxPrintf);
            #endif
        #endif

                tcp_send_timer_node_free(pstSendTimer);
            }
        }
        else
        {
    #if SUPPORT_PRINTF && DEBUG_LEVEL
        #if PRINTF_THREAD_MUTEX
            os_thread_mutex_lock(o_hMtxPrintf);
        #endif
            printf("The tcp sending timer queue is empty.\r\n");
        #if PRINTF_THREAD_MUTEX
            os_thread_mutex_unlock(o_hMtxPrintf);
        #endif
    #endif            
        }
        return TRUE; 
    }
    
    return FALSE; //* 没有要发送的数据了，返回FALSE            
}

static void tcp_link_ack_handler(PST_TCPLINK pstLink)
{        
    tcp_send_timer_lock();
    {
        PSTCB_TCPSENDTIMER pstSendTimer = pstLink->stcbSend.pstcbSndTimer;         
        while (pstSendTimer)
        {
            //* 大于确认序号，说明这之后的数据尚未成功送达对端，退出循环不再继续查找剩下的了
            if (pstSendTimer->unRight > pstLink->stLocal.unSeqNum - 1)
                break; 

            //* 执行到这里，说明当前报文已经成功送达对端，从当前链表队列中删除                        
            pstLink->stcbSend.pstcbSndTimer = pstSendTimer->pstcbNextForLink;
            pstLink->stcbSend.bSendPacketNum--;

            tcp_send_timer_node_del_unsafe(pstSendTimer);   //* 从定时器队列中删除当前节点                
            tcp_send_timer_node_free_unsafe(pstSendTimer);  //* 归还当前节点            

            //* 继续取出下一个节点            
            pstSendTimer = pstSendTimer->pstcbNextForLink;
        }
    }
    tcp_send_timer_unlock();    
}

void thread_tcp_handler(void *pvParam)
{
    INT nRtnVal; 
    BOOL blIsExistData; 
    PST_TCPLINK pstSndDataLink;   

    while (TRUE)
    {
        //* 等待信号到来
        nRtnVal = tcp_send_sem_pend(1);
        if (nRtnVal)
        {
            if (nRtnVal < 0)
            {
    #if SUPPORT_PRINTF && DEBUG_LEVEL
        #if PRINTF_THREAD_MUTEX
                os_thread_mutex_lock(o_hMtxPrintf);
        #endif
                printf("tcp_send_sem_pend() failed, %s\r\n", onps_error(ERRINVALIDSEM));
        #if PRINTF_THREAD_MUTEX
                os_thread_mutex_unlock(o_hMtxPrintf);
        #endif
    #endif
                os_sleep_secs(1);

                continue;
            }            
        }

        blIsExistData = FALSE;
        pstSndDataLink = NULL;

__lblSend:         
        //* 遍历所有tcp链路处理发送相关的逻辑        
        pstSndDataLink = tcp_link_for_send_data_get_next(pstSndDataLink); 
        if (pstSndDataLink)
        {
            //blIsExistData = TRUE; 

            //* 先看看状态是否还是CONNECTED状态
            if (pstSndDataLink->bState != TLSCONNECTED)
                goto __lblDelNode; 

            //* 先看看是否触发了快速重传（Fast Retransmit）机制或者存在sack选项吗？这两个有其一就应当重发数据
            //* 2022-12-17 下午开始发烧（这之后4天左右开始喉咙巨痛），2022-12-29基本康复，继续当前算法
            //* 将其转移到接收线程，一旦收到dup ack或sack直接回馈对应报文，而不是在这里收到信号后再去处理
            
            //* 看看ack序号到哪里了
            tcp_link_ack_handler(pstSndDataLink);            
            
            //* 发送数据
            if (pstSndDataLink->stcbSend.bSendPacketNum < 4)
            {
                if(tcp_link_send_data(pstSndDataLink))
                    blIsExistData = TRUE;
            }                                       

            goto __lblSend;

__lblDelNode: 
            //* 直接从数据队列中删除
            tcp_link_for_send_data_del(pstSndDataLink);
            pstSndDataLink = NULL; //* 重新开始取发送节点
            blIsExistData = FALSE; 
            goto __lblSend; 
        }
        else //* 到尾部了或者没有要发送数据的tcp链路了
        {            
            if (blIsExistData) //* 到尾部了
            {
                blIsExistData = FALSE; 
                goto __lblSend; 
            }
            else; //* 回到头部，等待下一个信号或者超时后再检查数据发送队列
        }
    }
}
#endif
