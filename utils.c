﻿#include "port/datatype.h"
#include "errors.h"
#include "port/sys_config.h"

#define SYMBOL_GLOBALS
#include "utils.h"
#undef SYMBOL_GLOBALS

CHAR *mem_char(CHAR *pszMem, CHAR ch, UINT unMemSize)
{
	CHAR *pszNext = pszMem;
	UINT i;

	for (i = 0; i < unMemSize; i++)
	{
		if (*(pszNext + i) == ch)
			return (pszNext + i);
	}

	return NULL;
}

CHAR *mem_str(CHAR *pszMem, CHAR *pszStr, UINT unStrSize, UINT unMemSize)
{
	const CHAR *pszNext = pszMem;

	pszNext = mem_char(pszNext, pszStr[0], unMemSize);
	while (pszNext != NULL)
	{
		if (strncmp((char *)pszNext, (char *)pszStr, unStrSize) == 0)
			return pszNext; 

		pszNext += 1;
		pszNext = mem_char(pszNext, (CHAR)pszStr[0], (pszMem + unMemSize) - pszNext);
	}

	return NULL;
}

USHORT tcpip_checksum(USHORT *pusData, INT nDataBytes)
{
	ULONG ulChecksum = 0;

	while (nDataBytes > 1)
	{
		ulChecksum += *pusData++;
		nDataBytes -= sizeof(USHORT);
	}

	if (nDataBytes)
	{
		ulChecksum += *(UCHAR*)pusData;
	}
	ulChecksum = (ulChecksum >> 16) + (ulChecksum & 0xffff);
	ulChecksum += (ulChecksum >> 16);

	return (USHORT)(~ulChecksum);
}

#if SUPPORT_PRINTF
void print_hex(const UCHAR *pubHex, USHORT usHexDataLen, UCHAR ubBytesPerLine)
{
	INT i;

	printf("%02X", pubHex[0]);
	for (i = 1; i < usHexDataLen; i++)
	{
		if (i % (INT)ubBytesPerLine)
			printf(" ");
		else
			printf("\r\n");
		printf("%02X", pubHex[i]);
	}

	printf("\r\n");
}
#endif