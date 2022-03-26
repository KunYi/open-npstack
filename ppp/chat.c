﻿#include "port/datatype.h"
#include "errors.h"
#include "port/sys_config.h"
#include "port/os_datatype.h"
#include "port/os_adapter.h"
#include "utils.h"

#if SUPPORT_PPP
#define SYMBOL_GLOBALS
#include "ppp/chat.h"
#undef SYMBOL_GLOBALS

//* 在此定义标准AT指令
//* ===============================================================================================
//* AT
#define AT				"AT"
#define AT_OK			"OK"
#define AT_ERROR		"ERROR"

//* ATE0，关闭回显指令
#define ATE0			"ATE0"
#define ATE_OK			"OK"
#define ATE_ERROR		"ERROR"

//* 检测SIM卡是否存在的指令
#define ATSIMTEST		"AT+SIMTEST?"
#define ATSIMTEST_OK	"OK"
#define ATSIMTEST_ERROR	"ERROR"

//* 检测modem已注册到移动网络
#define ATREG			"AT+CREG?"
#define ATREG_OK		"OK"
#define	ATREG_ERROR		"ERROR"
#define ATREG_MATCH		"+CREG:"

//* 检测modem已注册到移动网络（扩展），如果不存在扩展指令，则直接注释掉ATEREG宏即可
#define ATEREG			"AT+CEREG?"
#define ATEREG_OK		"OK"
#define	ATEREG_ERROR	"ERROR"
#define ATEREG_MATCH	"+CEREG:"

//* 设置APN
#define ATAPN			"AT+CGDCONT=1,\"IP\",\"4gnet\",,0,0"
#define ATAPN_OK		"OK"
#define ATAPN_ERROR		"ERROR"

//* 拨号
#define ATDIAL			"ATDT*99***1#"
#define ATDIAL_OK		"CONNECT"
#define ATDIAL_ERROR	"ERROR"
//* ===============================================================================================

static BOOL exec_at_cmd(HTTY hTTY, const CHAR *pszAT, UCHAR ubATBytes, const CHAR *pszOK, UCHAR ubOKBytes, const CHAR *pszErr,
						UCHAR ubErrBytes, CHAR *pszDataBuf, UINT unDataBufBytes, UCHAR ubWaitSecs, EN_ERROR_CODE *penErrCode)
{
	UINT unBytes;
	CHAR szBuf[64];
	UCHAR ubElapsedSecs = 0;
	UINT unHaveRcvBytes, unHaveCpyBytes, unCpyBytes;

	//* 发送AT指令
	sprintf(szBuf, "%s\r\n", pszAT); 
	unBytes = os_tty_send(hTTY, (UCHAR *)szBuf, (UINT)ubATBytes + 2);
	if (unBytes != (UINT)ubATBytes)
	{
		*penErrCode = ERRATWRITE;
		return FALSE;
	}

	//* AT指令执行一般至少需要1秒时长，所以这里先固定等待1秒后再去读modem
	os_sleep_secs(1); 

	//* 读取执行结果
	unHaveRcvBytes = 0;
	unHaveCpyBytes = 0;
	memset(szBuf, 0, sizeof(szBuf));
	while (ubElapsedSecs < ubWaitSecs && unHaveRcvBytes <= sizeof(szBuf) - 1)
	{	
		unBytes = os_tty_recv(hTTY, (UCHAR *)&szBuf[unHaveRcvBytes], sizeof(szBuf) - 1 - unHaveRcvBytes); 
		if (unBytes > 0) //* 收到了应答数据
		{
			if (pszDataBuf != NULL) //* 上层调用函数需要读取应答结果
			{
				if (unHaveCpyBytes < unDataBufBytes) //* 尚未填满上层调用函数提供的接收缓冲区，可以继续写入结果数据
				{
					UINT unRemainingBytes = unDataBufBytes - unHaveCpyBytes;
					unCpyBytes = (unRemainingBytes > unBytes) ? unBytes : unRemainingBytes;
					strncpy((char*)pszDataBuf + unHaveCpyBytes, &szBuf[unHaveRcvBytes], unCpyBytes);
					unHaveCpyBytes += unCpyBytes;
				}
			}
			else;

			unHaveRcvBytes += unBytes; 
			if (mem_str(szBuf, pszErr, ubErrBytes, unHaveRcvBytes))
			{
				*penErrCode = ERRATEXEC; 
				return FALSE; 
			}
			else if (mem_str(szBuf, pszOK, ubOKBytes, unHaveRcvBytes) != 0)
			{
				*penErrCode = ERRNO; 
				return TRUE;
			}
			else;
		}

		os_sleep_secs(1);
		ubElapsedSecs++;
	}

	*penErrCode = ERRATEXECTIMEOUT;
	return FALSE;
}

static BOOL sim_inserted(HTTY hTTY, EN_ERROR_CODE *penErrCode)
{
	BOOL blRtnVal;
	CHAR szRcvBuf[50], bCurState;
	UINT i, n, unReadBytes;
	const CHAR szMatchedStr[] = "+SIMTEST:";

	blRtnVal = exec_at_cmd(hTTY, ATSIMTEST, sizeof(ATSIMTEST) - 1, ATSIMTEST_OK, sizeof(ATSIMTEST_OK) - 1, ATSIMTEST_ERROR, sizeof(ATSIMTEST_ERROR) - 1, szRcvBuf, sizeof(szRcvBuf), 3, penErrCode);	
#if SUPPORT_PRINTF
	if (strlen(szRcvBuf))
		printf("%s", szRcvBuf);
#endif
	if (!blRtnVal)
		return FALSE;

	//* 解析读取到的数据，看是否已注册到当前的移动网络
	unReadBytes = strlen((char*)szRcvBuf);
	bCurState = 0;
	n = 0;
	for (i = 0; i<unReadBytes; i++)
	{
		switch (bCurState)
		{
		case 0:
			if (szRcvBuf[i] == szMatchedStr[n])
			{
				n++;

				//* 如果相等，则表明找到了匹配的数据
				if (n == sizeof(szMatchedStr) - 1)
					bCurState = 1;
				else;

			}
			else
				n = 0;
			break;

		case 1:
			if (szRcvBuf[i] > '0' && szRcvBuf[i] < '5')
				return TRUE;
			else
				return FALSE;

		default:
			break;
		}
	}

	return FALSE;
}

static BOOL reg_mobile_net(HTTY hTTY, const CHAR *pszATCmd, const CHAR *pszMatchedStr, EN_ERROR_CODE *penErrCode)
{
	BOOL blRtnVal;
	CHAR szRcvBuf[50];
	CHAR bCurState;
	UINT i, n, unReadBytes;
		
	blRtnVal = exec_at_cmd(hTTY, pszATCmd, strlen(pszATCmd), ATREG_OK, sizeof(ATREG_OK) - 1, ATREG_ERROR, sizeof(ATREG_ERROR) - 1, szRcvBuf, sizeof(szRcvBuf), 3, penErrCode);
#if SUPPORT_PRINTF
	if (strlen(szRcvBuf))
		printf("%s", szRcvBuf);
#endif
	if (!blRtnVal)
		return FALSE;

	//* 解析读取到的数据，看是否已注册到当前的移动网络
	UCHAR ubMatchedStrLen = (UCHAR)strlen(pszMatchedStr);
	unReadBytes = strlen((char*)szRcvBuf);
	bCurState = 0;
	n = 0;
	for (i = 0; i<unReadBytes; i++)
	{
		switch (bCurState)
		{
		case 0:
			if (szRcvBuf[i] == pszMatchedStr[n])
			{
				n++;

				//* 如果相等，则表明找到了匹配的数据
				if (n == ubMatchedStrLen)
					bCurState = 1;
				else;
			}
			else
				n = 0;
			break;

		case 1:
			if (szRcvBuf[i] == ',')
				bCurState = 2;
			break;

		case 2:
			if (szRcvBuf[i] == '1' || szRcvBuf[i] == '5')
				return TRUE;
			else
				return FALSE;

		default:
			break;
		}
	}

	return FALSE;
}

BOOL modem_ready(HTTY hTTY, EN_ERROR_CODE *penErrCode)
{
	CHAR szRcvBuf[64];
	UCHAR ubRetryNum;
	EN_ERROR_CODE enErrCode; 
	BOOL blRtnVal; 

	//* 循环执行AT指令，以待modem就绪
	//* ===========================================================================================
	ubRetryNum = 0;

__lblExecAT:
	ubRetryNum++;
	if (ubRetryNum > 10)
	{
#if SUPPORT_PRINTF
		printf("the command <%s> failed, %s\r\n", AT, error(enErrCode));
#endif
		return FALSE;
	}

	blRtnVal = exec_at_cmd(hTTY, AT, sizeof(AT) - 1, AT_OK, sizeof(AT_OK) - 1, AT_ERROR, sizeof(AT_ERROR) - 1, szRcvBuf, sizeof(szRcvBuf), 3, &enErrCode);
#if SUPPORT_PRINTF
	if (strlen(szRcvBuf))
		printf("%s", szRcvBuf);
#endif
	if (!blRtnVal)
	{
		os_sleep_secs(1);
		goto __lblExecAT;
	}
	//* ===========================================================================================

	//* 关闭回显
	//* ===========================================================================================
	ubRetryNum = 0;

__lblExecATE:
	ubRetryNum++;
	if (ubRetryNum > 3)
	{
#if SUPPORT_PRINTF
		printf("the command <%s> failed, %s\r\n", ATE0, error(enErrCode));
#endif
		return FALSE;
	}

	blRtnVal = exec_at_cmd(hTTY, ATE0, sizeof(ATE0) - 1, ATE_OK, sizeof(ATE_OK) - 1, ATE_ERROR, sizeof(ATE_ERROR) - 1, szRcvBuf, sizeof(szRcvBuf), 3, &enErrCode);
#if SUPPORT_PRINTF
	if (strlen(szRcvBuf))
		printf("%s", szRcvBuf);
#endif
	if (!blRtnVal)
	{
		os_sleep_secs(1);
		goto __lblExecATE;
	}
	//* ===========================================================================================

	//* 检测SIM卡是否存在
	//* ===========================================================================================
	ubRetryNum = 0;

__lblSIMTest:
	ubRetryNum++;
	if (ubRetryNum > 3)	//* 重试多次
	{
		if (ERRNO == enErrCode)
			enErrCode = ERRSIMCARD;
#if SUPPORT_PRINTF
		printf("the command <%s> failed, %s\r\n", ATSIMTEST, error(enErrCode));
#endif
		return FALSE;
	}

	if (!sim_inserted(hTTY, &enErrCode))
	{
		os_sleep_secs(1);
		goto __lblSIMTest;
	}
	//* ===========================================================================================

	//* 注册到移动运营商网络，理论上需要进行标准（REG）和扩展（EREG）两个判断，某些地方只有其中一种指令有效
	//* ===========================================================================================
	ubRetryNum = 0;

__lblRegMobileNet:
	ubRetryNum++;
	if (ubRetryNum > 90) //* 不断查询，直至到指定时长后再注册不到移动网络就返回错误	
	{
		if (ERRNO == enErrCode)
			enErrCode = ERRREGMOBILENET;
#if SUPPORT_PRINTF
		printf("the command <%s> failed, %s\r\n", ATREG, error(enErrCode));
#endif
		return FALSE; 
	}

	if (!reg_mobile_net(hTTY, ATREG, ATREG_MATCH, &enErrCode))
	{
#ifdef ATEREG
		if (!reg_mobile_net(hTTY, ATEREG, ATEREG_MATCH, &enErrCode))
		{
#endif
			os_sleep_secs(1);
			goto __lblRegMobileNet;
#ifdef ATEREG
		}
#endif
	}
	//* ===========================================================================================

	return TRUE;
}

BOOL modem_dial(HTTY hTTY, EN_ERROR_CODE *penErrCode)
{
	CHAR szRcvBuf[64];
	UCHAR ubRetryNum;
	EN_ERROR_CODE enErrCode;
	BOOL blRtnVal;

#ifdef ATAPN
	//* 设置APN
	//* ===========================================================================================
	ubRetryNum = 0;

__lblExecAT:
	ubRetryNum++;
	if (ubRetryNum > 3)
	{
#if SUPPORT_PRINTF
		printf("the command <%s> failed, %s\r\n", ATAPN, error(enErrCode));
#endif
		return FALSE;
	}

	blRtnVal = exec_at_cmd(hTTY, ATAPN, sizeof(ATAPN) - 1, ATAPN_OK, sizeof(ATAPN_OK) - 1, ATAPN_ERROR, sizeof(ATAPN_ERROR) - 1, szRcvBuf, sizeof(szRcvBuf), 3, &enErrCode);
#if SUPPORT_PRINTF
	if (strlen(szRcvBuf))
		printf("%s", szRcvBuf);
#endif
	if (!blRtnVal)
	{
		os_sleep_secs(1);
		goto __lblExecAT;
	}
	//* ===========================================================================================
#endif

	//* 拨号
	blRtnVal = exec_at_cmd(hTTY, ATDIAL, sizeof(ATDIAL) - 1, ATDIAL_OK, sizeof(ATDIAL_OK) - 1, ATDIAL_ERROR, sizeof(ATDIAL_ERROR) - 1, szRcvBuf, sizeof(szRcvBuf), 3, &enErrCode);
#if SUPPORT_PRINTF
	if (strlen(szRcvBuf))
		printf("%s", szRcvBuf);
#endif
	if (!blRtnVal)
	{
		printf("the command <%s> failed, %s\r\n", ATDIAL, error(enErrCode));
		return FALSE;
	}

	return TRUE; 
}

#endif