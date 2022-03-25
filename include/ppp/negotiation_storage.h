﻿/* negotiation_storage.h
 *
 * 用于保存链路协商结果的存储类结构体及其相关辅助宏定义
 * Neo-T, 创建于2022.03.25 10:26
 * 版本: 1.0
 *
 */
#ifndef NEGOTIATION_STORAGE_H
#define NEGOTIATION_STORAGE_H

#ifdef SYMBOL_GLOBALS
	#define NEGOTIATION_STORAGE_EXT
#else
	#define NEGOTIATION_STORAGE_EXT extern
#endif //* SYMBOL_GLOBALS
#include "ppp_frame.h"

 //* ppp链路工作状态
typedef enum {
	TTYINIT = 0,		//* tty终端初始化
	STARTNEGOTIATION,	//* 开启链路协商
	NEGOTIATION,		//* 协商
	ESTABLISHED,		//* 链路已建立
	TERMINATE,			//* 终止链路
	TERMINATED			//* 链路已终止
} EN_PPP_LINK_STATE;

//* PPP控制块
typedef struct _STCB_NETIFPPP_ {
	HTTY hTTY;
	EN_PPP_LINK_STATE enState;
	BOOL blIsThreadExit;
} STCB_NETIFPPP, *PSTCB_NETIFPPP;

//* 记录协商结果
typedef struct _ST_NEGORESULT_ {
	struct {
		UINT unMagicNum;
		USHORT usMRU;
		UINT unACCM;
		struct {
			USHORT usType;
			UCHAR ubaData[16];
		} stAuth;
		BOOL blIsProtoComp;
		BOOL blIsAddrCtlComp;
		BOOL blIsNegoValOfProtoComp;
		BOOL blIsNegoValOfAddrCtlComp;
		BOOL blIsLCPNegoEnd;
	} stLCP;
	struct {
		UINT unAddr;
		UINT unPrimaryDNSAddr;
		UINT unSecondaryDNSAddr;
		UINT unPointToPointAddr;
		UINT unNetMask;
	} stIPCP;
} ST_NEGORESULT, *PST_NEGORESULT;

#endif