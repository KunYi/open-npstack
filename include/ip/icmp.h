/* icmp.h
 *
 * icmp协议相关功能函数
 *
 * Neo-T, 创建于2022.04.12 14:10
 * 版本: 1.0
 *
 */
#ifndef ICMP_H
#define ICMP_H

#ifdef SYMBOL_GLOBALS
	#define ICMP_EXT
#else
	#define ICMP_EXT extern
#endif //* SYMBOL_GLOBALS
#include "icmp_frame.h"

typedef struct _ST_ICMP_REPORT_RESULT_ { //* 对端通过icmp报文送达的相关发送结果
    UCHAR ubType; 
    UCHAR ubCode; 
    UCHAR ubProtocol; 
    UINT unSrcAddr; 
    UINT unDstAddr; 
} ST_ICMP_REPORT_RESULT, *PST_ICMP_REPORT_RESULT;

ICMP_EXT INT icmp_send_echo_reqest(INT nInput, USHORT usIdentifier, USHORT usSeqNum, UCHAR ubTTL, in_addr_t unDstAddr, UCHAR *pubData, UINT unDataSize, EN_ONPSERR *penErr);
ICMP_EXT void icmp_recv(UCHAR *pubPacket, INT nPacketLen); //* 接收函数
ICMP_EXT void icmp_get_last_report(PST_ICMP_REPORT_RESULT pstResult);
ICMP_EXT const CHAR *icmp_get_description(UCHAR ubType, UCHAR ubCode);


#endif
