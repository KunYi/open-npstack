/*
 * 版权属于onps栈开发团队，遵循Apache License 2.0开源许可协议
 *
 */
#include "port/datatype.h"
#include "onps_errors.h"
#include "port/sys_config.h"
#include "port/os_datatype.h"
#include "port/os_adapter.h"
#include "onps_utils.h"
#include "ip/tcp_frame.h"
#define SYMBOL_GLOBALS
#include "ip/tcp_options.h"
#undef SYMBOL_GLOBALS
#include "ip/tcp_link.h"

//* 系统支持的TCP选项列表，在TCP连接协商时（无论是作为服务器还是客户端）SYN或SYN ACK报文携带如下选项值，
//* 如果要增加支持的选项，直接在该结构体数组增加要支持的选项即可
typedef struct _ST_TCPOPT_HANDLER_ {
    EN_TCPOPTTYPE enType;
    UCHAR ubLen;
    CHAR bIsNeedAttach; //* 只有本地支持的tcp选项该字段置TRUE，否则置FALSE
    void(*pfunAttach)(UCHAR *pubAttachAddr);
    void(*pfunPut)(PST_TCPLINK pstLink, UCHAR *pubOption); 
} ST_TCPOPT_HANDLER, *PST_TCPOPT_HANDLER;
static void tcp_options_attach_mss(UCHAR *pubAttachAddr); 
static void tcp_options_attach_wndscale(UCHAR *pubAttachAddr); 
static void tcp_options_put_mss(PST_TCPLINK pstLink, UCHAR *pubOption); 
static void tcp_options_put_wnd_scale(PST_TCPLINK pstLink, UCHAR *pubOption);
static void tcp_options_put_sack(PST_TCPLINK pstLink, UCHAR *pubOption);
const static ST_TCPOPT_HANDLER lr_staTcpOptList[] =
{    
    { TCPOPT_MSS, (UCHAR)sizeof(ST_TCPOPT_MSS), TRUE, tcp_options_attach_mss, tcp_options_put_mss }, //* 最大报文长度(MSS)
    { TCPOPT_WNDSCALE, (UCHAR)sizeof(ST_TCPOPT_WNDSCALE), TRUE, tcp_options_attach_wndscale, tcp_options_put_wnd_scale }, //* 窗口扩大因子
#if SUPPORT_SACK
    { TCPOPT_SACK, (UCHAR)sizeof(ST_TCPOPT_HDR), TRUE, NULL, tcp_options_put_sack }, //* 是否支持SACK
#endif

    //* 以下为不需要挂载的tcp选项，一定要放到下面，需要挂载的放到上面    
#if !SUPPORT_SACK
    { TCPOPT_SACK, (UCHAR)sizeof(ST_TCPOPT_HDR), FALSE }, //* 是否支持SACK
#endif
    { TCPOPT_TIMESTAMP, (UCHAR)sizeof(ST_TCPOPT_TIMESTAMP), FALSE },
};

static void tcp_options_attach_mss(UCHAR *pubAttachAddr)
{
    USHORT usMss = TCPRCVBUF_SIZE - sizeof(ST_TCP_HDR) - TCP_OPTIONS_SIZE_MAX;
    pubAttachAddr[0] = ((UCHAR *)&usMss)[1]; 
    pubAttachAddr[1] = ((UCHAR *)&usMss)[0];    
}

static void tcp_options_attach_wndscale(UCHAR *pubAttachAddr)
{
    *((CHAR *)pubAttachAddr) = TCP_WINDOW_SCALE; 
}

INT tcp_options_attach(UCHAR *pubAttachAddr, INT nAttachBufSize)
{
    INT i, nHasAttachBytes = 0; 
    UCHAR ubNopNum = 0; 
    PST_TCPOPT_HDR pstOptHdr;
    for (i = 0; i < (INT)(sizeof(lr_staTcpOptList) / sizeof(ST_TCPOPT_HANDLER)); i++)
    {
        if (!lr_staTcpOptList[i].bIsNeedAttach) //* 到头了，前面的lr_staTcpOptList数组确保假值就意味着已经挂载结束
            break; 

        if (i)
        {
            //* 要确保tcp选项为4字节对齐，不对齐时填充对应字节数的nop字符以强制对齐
            ubNopNum = lr_staTcpOptList[i].ubLen % 4 ? 4 - (lr_staTcpOptList[i].ubLen % 4) : 0;
        }                
        if (nHasAttachBytes + (INT)lr_staTcpOptList[i].ubLen + ubNopNum > nAttachBufSize)
            return nHasAttachBytes; 

        //* 先填充nop字符以强制4字节对齐
        if (ubNopNum)
        {
            UCHAR k = 0;
            UCHAR *pubNop = pubAttachAddr + nHasAttachBytes;
            for (; k < ubNopNum; k++)
                *(pubNop + k) = TCPOPT_NOP;
            nHasAttachBytes += (INT)ubNopNum;
        }

        //* 附着协议栈支持的tcp选项
        pstOptHdr = (PST_TCPOPT_HDR)(pubAttachAddr + nHasAttachBytes); 
        pstOptHdr->ubKind = lr_staTcpOptList[i].enType; 
        pstOptHdr->ubLen = lr_staTcpOptList[i].ubLen; 
        if (lr_staTcpOptList[i].pfunAttach)
            lr_staTcpOptList[i].pfunAttach(((UCHAR *)pstOptHdr) + sizeof(ST_TCPOPT_HDR));         

        nHasAttachBytes += (INT)lr_staTcpOptList[i].ubLen; 
    }

    return nHasAttachBytes; 
}

static void tcp_options_put_mss(PST_TCPLINK pstLink, UCHAR *pubOption)
{
    PST_TCPOPT_MSS pstOption = (PST_TCPOPT_MSS)pubOption; 
    pstLink->stPeer.usMSS = htons(pstOption->usValue); 
}

static void tcp_options_put_wnd_scale(PST_TCPLINK pstLink, UCHAR *pubOption)
{
    PST_TCPOPT_WNDSCALE pstOption = (PST_TCPOPT_WNDSCALE)pubOption;
    pstLink->stPeer.bWndScale = pstOption->bScale;
}

static void tcp_options_put_sack(PST_TCPLINK pstLink, UCHAR *pubOption)
{
#if SUPPORT_SACK
    pubOption = pubOption; 
    pstLink->stPeer.bSackEn = TRUE; 
#endif
}

void tcp_options_get(PST_TCPLINK pstLink, UCHAR *pubOptions, INT nOptionsLen)
{
    INT nReadBytes = 0; 
    UCHAR *pubCurOption;
    while (nReadBytes < nOptionsLen)
    {                
        pubCurOption = pubOptions + nReadBytes;       

        //* 先判断是否为nop或end，如果是则直接跳过
        EN_TCPOPTTYPE enType = (EN_TCPOPTTYPE)(*pubCurOption);
        if (enType == TCPOPT_NOP || enType == TCPOPT_END)
        {
            nReadBytes += 1;
            continue; 
        }
        
        BOOL blIsNotFound = TRUE;
        INT i;
        for (i = 0; i < (INT)(sizeof(lr_staTcpOptList) / sizeof(ST_TCPOPT_HANDLER)); i++)
        {                       
            if (enType == lr_staTcpOptList[i].enType)
            {
                if(lr_staTcpOptList[i].pfunPut)
                    lr_staTcpOptList[i].pfunPut(pstLink, pubCurOption);
                nReadBytes += (INT)lr_staTcpOptList[i].ubLen;

                blIsNotFound = FALSE; 
                break; 
            }
        }

        if (blIsNotFound)
        {
    #if SUPPORT_PRINTF && DEBUG_LEVEL
        #if PRINTF_THREAD_MUTEX
            os_thread_mutex_lock(o_hMtxPrintf);
        #endif            
            printf("Unknown tcp option %02X\r\n", pubCurOption[0]);
        #if PRINTF_THREAD_MUTEX
            os_thread_mutex_unlock(o_hMtxPrintf);
        #endif
    #endif
            return; 
        }        
    }
}
