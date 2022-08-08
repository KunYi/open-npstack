/* socket.h
 *
 * 伯克利套接字（Berkeley sockets）非标准且不完全实现，按照传统socket编程思想并结合实际应用经验实现的用户层TCP、UDP通讯接口函数，简化了
 * 传统BSD socket需要的一些繁琐的操作，将一些不必要的操作细节改为底层实现，比如select模型、阻塞及非阻塞读写操作等
 * 
 * Neo-T, 创建于2022.04.26 10:26
 * 版本: 1.0
 *
 */
#ifndef SOCKET_H
#define SOCKET_H

#ifdef SYMBOL_GLOBALS
	#define SOCKET_EXT
#else
	#define SOCKET_EXT extern
#endif //* SYMBOL_GLOBALS
#include "onps_input.h"

typedef INT SOCKET;         //* socket句柄
#define INVALID_SOCKET  -1  //* 无效的SOCKET

//* Supported address families.
#define AF_INET 2   //* internetwork: UDP, TCP, etc.

//* Socket types.
#define SOCK_STREAM 1   //* TCP, stream (connection) socket
#define SOCK_DGRAM  2   //* UDP, datagram (conn.less) socket

//* 参数family仅支持AF_INET，其它不支持，type仅支持SOCK_STREAM、SOCK_DGRAM两种协议，protocol固定为0
SOCKET_EXT SOCKET socket(INT family, INT type, INT protocol, EN_ONPSERR *penErr); 
SOCKET_EXT void close(SOCKET socket);

//* 连接函数，阻塞型，直至连接成功或超时，返回0意味着连接成功，-1则意味着连接失败，具体的错误信息通过onps_get_last_error()函数
//* 获得，注意，nConnTimeout参数必须大于0，小于等于0则使用缺省超时时间TCP_CONN_TIMEOUT
SOCKET_EXT INT connect(SOCKET socket, const CHAR *srv_ip, USHORT srv_port, INT nConnTimeout);
//* 非阻塞连接函数，连接成功返回0，连接中会一直返回1，返回-1则意味着连接失败，具体的错误信息通过onps_get_last_error()函数获得
SOCKET_EXT INT connect_nb(SOCKET socket, const CHAR *srv_ip, USHORT srv_port); 

//* 发送函数(阻塞型)，直至收到tcp层的ack报文或者超时才会返回，返回值大于0为实际发送的字节数，小于0则发送失败，具体错误信息通过onps_get_last_error()函数获得
//* 对于udp协议来说参数nWaitAckTimeout将被忽略，其将作为非阻塞型函数使用
SOCKET_EXT INT send(SOCKET socket, UCHAR *pubData, INT nDataLen, INT nWaitAckTimeout); 
//* 发送函数(非阻塞型)，返回值大于0则为实际发送成功的字节数，等于0为发送中，尚未收到对端的应答，小于0则发送失败，具体错误信息通过onps_get_last_error()函数获得
//* udp协议该函数与send()函数功能及实现逻辑完全相同
SOCKET_EXT INT send_nb(SOCKET socket, UCHAR *pubData, INT nDataLen);
//* 仅用于udp发送，发送时指定目标地址
SOCKET_EXT INT sendto(SOCKET socket, const CHAR *srv_ip, USHORT srv_port, UCHAR *pubData, INT nDataLen);

//* 设定recv()函数等待接收的时长（单位：秒），大于0指定数据到达的最长等待时间；0，则不等待；-1，则一直等待直至数据到达或报错
SOCKET_EXT BOOL socket_set_rcv_timeout(SOCKET socket, CHAR bRcvTimeout, EN_ONPSERR *penErr);

//* 接收函数(阻塞型/非阻塞型)，依赖于socket_set_rcv_timeout()函数设定的接收等待时长，缺省为一直等待直至收到数据或报错，阻塞型返回值为实际收到的数据长度，-1则代
//* 表出错；非阻塞型返回值为实际收到的数据长度（大于等于0），-1同样代表接收失败
SOCKET_EXT INT recv(SOCKET socket, UCHAR *pubDataBuf, INT nDataBufSize); 

//* 接收函数，仅用于udp协议接收
SOCKET_EXT INT recvfrom(SOCKET socket, UCHAR *pubDataBuf, INT nDataBufSize, in_addr_t *punFromIP, USHORT *pusFromPort);

//* 获取当前tcp连接状态，0：未连接；1：已连接；-1：读取状态失败，具体错误信息由参数penErr返回
SOCKET_EXT INT is_tcp_connected(SOCKET socket, EN_ONPSERR *penErr); 

//* 为socket绑定指定的网络地址和端口，如果想绑定任意网络接口地址，参数pszNetifIp为NULL即可
SOCKET_EXT INT bind(SOCKET socket, const CHAR *pszNetifIp, USHORT usPort);

//* tcp服务器进入被动监听状态，入口参数与函数功能与伯克利sockets完全相同
SOCKET_EXT INT listen(SOCKET socket, INT backlog); 

#endif
