﻿#include "port/datatype.h"
#include "port/os_datatype.h"
#include "timer.h"

#define SYMBOL_GLOBALS
#include "port/os_adapter.h"
#undef SYMBOL_GLOBALS

//* 协议栈内部工作线程列表
extern void THIPReceiver(void *pvParam); 
STCB_PSTACKTHREAD o_stcbaPStackThread[] = {
	{ THTimerCount, NULL}, 
	{ THTimeoutHandler, NULL }, 
	{ THIPReceiver, NULL }
}; 

/* 用户自定义变量声明区 */
/* …… */

void os_thread_pstack_start(void *pvParam)
{
	/* 用户自定义代码 */
	/* …… */

	//* 建立工作线程
	INT i; 
	for (i = 0; i < sizeof(o_stcbaPStackThread) / sizeof(STCB_PSTACKTHREAD); i++)
	{
		//* 在此按照顺序建立工作线程
	}

	/* 用户自定义代码 */
	/* …… */
}

HMUTEX os_thread_mutex_init(void)
{
	/* 用户自定义代码 */
	/* …… */
	return INVALID_HMUTEX; //* 初始失败要返回一个无效句柄
}

void os_thread_mutex_lock(HMUTEX hMutex)
{
	/* 用户自定义代码 */
	/* …… */
}

void os_thread_mutex_unlock(HMUTEX hMutex)
{
	/* 用户自定义代码 */
	/* …… */
}

HSEM os_thread_sem_init(UCHAR ubSemCount)
{
	/* 用户自定义代码 */
	/* …… */

	return INVALID_HSEM; //* 初始失败要返回一个无效句柄
}

void os_thread_sem_post(HSEM hSem)
{
	/* 用户自定义代码 */
	/* …… */
}

void os_thread_sem_pend(HSEM hSem, UINT unWaitSecs)
{
	/* 用户自定义代码 */
	/* …… */
}