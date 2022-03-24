﻿/* errors.h
 *
 * 错误类型定义
 *
 * Neo-T, 创建于2022.03.14 17:14
 * 版本: 1.0
 *
 */
#ifndef ERRORS_H
#define ERRORS_H

#ifdef SYMBOL_GLOBALS
#define ERRORS_EXT
#else
#define ERRORS_EXT extern
#endif //* SYMBOL_GLOBALS

#define ERROR_NUM	20
typedef enum {
	ERRNO = 0,			//* 没有发生任何错误
	ERRNOPAGENODES,		//* 无可用的内存页面节点
	ERRREQMEMTOOLARGE,	//* 申请的内存过大，超过了系统支持的最大申请配额
	ERRNOFREEMEM,		//* 系统已无可用内存
	ERRMUTEXINITFAILED, //* 线程同步锁初始化失败
	ERRNOBUFLISTNODE, 	//* 无缓冲区链表节点
	ERRSEMINITFAILED,	//* 信号量初始化失败
	ERROPENTTY,			//* tty终端打开失败
	ERRATWRITE,			//* AT指令写错误
	ERRATEXEC,			//* AT指令返回错误
	ERRATEXECTIMEOUT,	//* AT指令执行超时
	ERRSIMCARD,			//* 未检测到SIM卡
	ERRREGMOBILENET,	//* 没有注册到移动网络
	ERRPPPDELIMITER,	//* 未找到ppp帧定界符
	ERRTOOMANYTTYS,		//* tty数量过多
	ERRTTYHANDLE,		//* tty句柄无效
	ERROSADAPTER,		//* os适配层错误
} EN_ERROR_CODE;

typedef struct _ST_ERROR_ {
	EN_ERROR_CODE enCode; 
	CHAR szDesc[128];
} ST_ERROR, *PST_ERROR;

ERRORS_EXT const CHAR *error(EN_ERROR_CODE enErrCode);

#endif

