/*
 * 版权属于onps栈开发团队，遵循Apache License 2.0开源许可协议
 *
 * icmp/tcp/udp层接收处理逻辑用到的基础数据结构、辅助宏、相关功能函数等的定义与声明工作
 *
 * Neo-T, 创建于2022.04.13 17:25
 *
 */
#ifndef ONPSINPUT_H
#define ONPSINPUT_H

#ifdef SYMBOL_GLOBALS
	#define ONPSINPUT_EXT
#else
	#define ONPSINPUT_EXT extern
#endif //* SYMBOL_GLOBALS
#include "ip/ip.h"

//* 协议栈支持的输入控制块相关配置项定义
typedef enum {    
    IOPT_SETICMPECHOID = 0,     //* 设置icmp echo请求ID
    IOPT_SETTCPUDPADDR,         //* 设置TCP/UDP本地分配的地址
    IOPT_SET_TCP_LINK_FLAGS,    //* 设置tcp链路标志
    IOPT_GETTCPUDPADDR,         //* 获取TCP/UDP本地分配的地址
    IOPT_GETSEM,                //* 获取input用到的semaphore
    IOPT_GETIPPROTO,            //* 获取当前input绑定的ip上层协议
    IOPT_GETTCPLINKSTATE,       //* 获取tcp链路状态
    IOPT_SETTCPLINKSTATE,       //* 设置tcp链路状态
    IOPT_SETATTACH,             //* 设置附加信息
    IOPT_GETATTACH,             //* 获取附加信息地址
    IOPT_GETTCPUDPLINK,         //* 获取tcp/udp链路状态
    IOPT_GETTCPDATASNDSTATE,    //* 获取tcp链路数据发送的状态
    IOPT_SETRCVTIMEOUT,         //* 设置接收等待时长（单位：秒）
    IOPT_GETRCVTIMEOUT,         //* 获取接收等待时长
    IOPT_GETLASTSNDBYTES,       //* 获取最近一次数据发送长度        
    IOPT_GET_TCP_LINK_FLAGS,    //* 读取tcp链路标志
} ONPSIOPT;

#define TCP_TYPE_LCLIENT 0  //* 连接远端服务器的tcp客户端
#define TCP_TYPE_RCLIENT 1  //* 连接本地服务器的tcp客户端
#define TCP_TYPE_SERVER  2  //* 本地tcp服务器
typedef struct _ST_TCPUDP_HANDLE_ {
    CHAR bType;    //* 仅用于tcp链路，udp链路忽略该字段，用于标识这是否是服务器、连接本地服务器的客户端、连接远端服务器的客户端（udp客户端与服务器的处理逻辑本质上完全相同，不需要单独区分）    
    USHORT usPort;
    UINT unNetifIp;         
} ST_TCPUDP_HANDLE, *PST_TCPUDP_HANDLE;

typedef struct _ST_TCPLINK_ ST_TCPLINK, *PST_TCPLINK; 

//* 输入控制块初始化
ONPSINPUT_EXT BOOL onps_input_init(EN_ONPSERR *penErr); 

//* 去初始化输入控制块
ONPSINPUT_EXT void onps_input_uninit(void); 

//* 建立一个新的输入控制块
ONPSINPUT_EXT INT onps_input_new(EN_IPPROTO enProtocol, EN_ONPSERR *penErr); 
#if SUPPORT_ETHERNET
ONPSINPUT_EXT INT onps_input_new_tcp_remote_client(INT nInputSrv, USHORT usSrvPort, in_addr_t unSrvIp, USHORT usCltPort, in_addr_t unCltIp, PST_TCPLINK *ppstTcpLink, EN_ONPSERR *penErr);
#endif

//* 释放一个输入控制块
ONPSINPUT_EXT void onps_input_free(INT nInput); 

//* 设置/获取相关配置项
ONPSINPUT_EXT BOOL onps_input_set(INT nInput, ONPSIOPT enInputOpt, void *pvVal, EN_ONPSERR *penErr);
ONPSINPUT_EXT BOOL onps_input_get(INT nInput, ONPSIOPT enInputOpt, void *pvVal, EN_ONPSERR *penErr);

//* 投递一个数据到达或链路异常的信号
ONPSINPUT_EXT void onps_input_sem_post(INT nInput); 
ONPSINPUT_EXT INT onps_input_sem_pend(INT nInput, INT nWaitSecs, EN_ONPSERR *penErr);
ONPSINPUT_EXT INT onps_input_sem_pend_uncond(INT nInput, INT nWaitSecs, EN_ONPSERR *penErr);

#if SUPPORT_ETHERNET
ONPSINPUT_EXT void onps_input_sem_post_tcpsrv_accept(INT nSrvInput, INT nCltInput, UINT unLocalSeqNum); 
#endif

//* 对tcp链路关闭状态进行迁移
ONPSINPUT_EXT BOOL onps_input_set_tcp_close_state(INT nInput, CHAR bDstState); 
//* tcp链路关闭操作定时器计数函数
ONPSINPUT_EXT INT onps_input_tcp_close_time_count(INT nInput);

//* input层未tcp之类的ip上层数据流协议提供的线程锁，确保发送序号不出现乱序的情形
ONPSINPUT_EXT void onps_input_lock(INT nInput); 
ONPSINPUT_EXT void onps_input_unlock(INT nInput);

//* 根据对端发送的标识获取本地icmp句柄
ONPSINPUT_EXT INT onps_input_get_icmp(USHORT usIdentifier);

//* 将底层协议收到的对端发送过来的数据放入接收缓冲区
ONPSINPUT_EXT BOOL onps_input_recv(INT nInput, const UCHAR *pubData, INT nDataBytes, in_addr_t unFromIP, USHORT usFromPort, EN_ONPSERR *penErr);
ONPSINPUT_EXT INT onps_input_tcp_recv(INT nInput, const UCHAR *pubData, INT nDataBytes, EN_ONPSERR *penErr);

//* 将收到的数据推送给用户层
ONPSINPUT_EXT INT onps_input_recv_upper(INT nInput, UCHAR *pubDataBuf, UINT unDataBufSize, in_addr_t *punFromIP, USHORT *pusFromPort, EN_ONPSERR *penErr);

//* 等待接收icmp层对端发送的数据
ONPSINPUT_EXT INT onps_input_recv_icmp(INT nInput, UCHAR **ppubPacket, UINT *punSrcAddr, UCHAR *pubTTL, INT nWaitSecs); 

//* 检查要某个端口是否已被使用              
ONPSINPUT_EXT BOOL onps_input_port_used(EN_IPPROTO enProtocol, USHORT usPort);

//* 分配一个动态端口
ONPSINPUT_EXT USHORT onps_input_port_new(EN_IPPROTO enProtocol);

//* 根据ip地址和端口号获取input句柄
#if SUPPORT_ETHERNET
ONPSINPUT_EXT INT onps_input_get_handle_of_tcp_rclient(UINT unSrvIp, USHORT usSrvPort, UINT unCltIp, USHORT usCltPort, PST_TCPLINK *ppstTcpLink); 
#endif
ONPSINPUT_EXT INT onps_input_get_handle(EN_IPPROTO enIpProto, UINT unNetifIp, USHORT usPort, void *pvAttach);

//* 设置/获取最近一次发生的错误
ONPSINPUT_EXT const CHAR *onps_get_last_error(INT nInput, EN_ONPSERR *penErr);
ONPSINPUT_EXT EN_ONPSERR onps_get_last_error_code(INT nInput);
ONPSINPUT_EXT void onps_set_last_error(INT nInput, EN_ONPSERR enErr);

#if SUPPORT_SACK
ONPSINPUT_EXT INT onps_tcp_send(INT nInput, UCHAR *pubData, INT nDataLen);
#endif

#endif
