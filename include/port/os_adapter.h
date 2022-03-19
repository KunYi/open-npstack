﻿/* os_adapter.h
*
* 操作系统适配层，在此定义与目标操作系统相关的接口函数
*
* Neo-T, 创建于2022.03.15 13:53
* 版本: 1.0
*
*/
#ifndef OS_ADAPTER_H
#define OS_ADAPTER_H

#ifdef SYMBOL_GLOBALS
#define OS_ADAPTER_EXT
#else
#define OS_ADAPTER_EXT extern
#endif //* SYMBOL_GLOBALS

//* 一些协议栈要用到的与OS适配层相关的全局变量、函数、结构体定义
//* ==============================================================================================
typedef struct _STCB_PSTACKTHREAD_ { //* 协议栈内部工作线程控制块，其用于线程建立
	void(*pfunThread)(void *pvParam); 
	void *pvParam; 
} STCB_PSTACKTHREAD, *PSTCB_PSTACKTHREAD;
extern STCB_PSTACKTHREAD o_stcbaPStackThread[];
//* ==============================================================================================

//* 一些协议栈要用到的需要OS提供的支撑函数
//* ==============================================================================================
OS_ADAPTER_EXT void os_sleep_secs(UINT unSecs);							//* 休眠，单位：秒 
OS_ADAPTER_EXT HMUTEX os_thread_mutex_init(void);						//* 线程同步锁初始化，成功返回同步锁句柄，失败则返回INVALID_HMUTEX
OS_ADAPTER_EXT void os_thread_mutex_lock(HMUTEX hMutex);				//* 线程同步区加锁
OS_ADAPTER_EXT void os_thread_mutex_unlock(HMUTEX hMutex);				//* 线程同步区开锁
OS_ADAPTER_EXT void os_thread_mutex_uninit(HMUTEX hMutex);				//* 删除线程同步锁，释放该资源
OS_ADAPTER_EXT HSEM os_thread_sem_init(UINT unInitVal, UINT unCount);	//* 信号量初始化，参数unInitVal指定初始信号量值， unCount指定信号量最大数值
OS_ADAPTER_EXT void os_thread_sem_post(HSEM hSem);						//* 投递信号量
OS_ADAPTER_EXT INT os_thread_sem_pend(HSEM hSem, UINT unWaitSecs);		//* 等待信号量到达，参数unWaitSecs指定要等待的超时时间（单位为秒）：0，一直等下去直至信号量到达，返回值始终未0；其它，等待指定时间，如果指定时间内信号量到达，则返回值为0，超时则返回值为-1
OS_ADAPTER_EXT void os_thread_sem_uninit(HSEM hSem);					//* 信号量去初始化，释放该资源
OS_ADAPTER_EXT void os_thread_pstack_start(void *pvParam);				//* 协议栈内部工作线程启动

#endif

