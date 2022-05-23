/* tcp_options.h
 *
 * tcp头部选项相关的功能函数实现
 *
 * Neo-T, 创建于2022.04.29 10:41
 * 版本: 1.0
 *
 */
#ifndef TCP_OPTIONS_H
#define TCP_OPTIONS_H

#ifdef SYMBOL_GLOBALS
	#define TCP_OPTIONS_EXT
#else
	#define TCP_OPTIONS_EXT extern
#endif //* SYMBOL_GLOBALS

#define TCP_OPTIONS_SIZE_MAX 40 //* 最长40字节的tcp选项

typedef enum {
    TCPOPT_END = 0,         //* 选项表结束
    TCPOPT_NOP = 1,         //* 无操作
    TCPOPT_MSS = 2,         //* 最大报文长度(MSS)
    TCPOPT_WNDSCALE = 3,    //* 窗口扩大因子
    TCPOPT_SACK = 4,        //* SACK
    TCPOPT_TIMESTAMP = 8,   //* 时间戳
} EN_TCPOPTTYPE;

typedef struct _ST_TCPLINK_ ST_TCPLINK, *PST_TCPLINK;
TCP_OPTIONS_EXT INT tcp_options_attach(UCHAR *pubAttachAddr, INT nAttachBufSize); 
TCP_OPTIONS_EXT void tcp_options_get(PST_TCPLINK pstLink, UCHAR *pubOptions, INT nOptionsLen);

#endif