/*
 * 版权属于onps栈开发团队，遵循Apache License 2.0开源许可协议
 *
 * ip帧结构定义
 *
 * Neo-T, 创建于2022.04.12 10:24
 *
 */
#ifndef IP_FRAME_H
#define IP_FRAME_H

#ifdef SYMBOL_GLOBALS
	#define IP_FRAME_EXT
#else
	#define IP_FRAME_EXT extern
#endif //* SYMBOL_GLOBALS

typedef enum {
    IPPROTO_ICMP = 1,   //* Internet Control Message Protocol
    IPPROTO_IGMP = 2,   //* Internet Gateway Management Protocol
    IPPROTO_TCP  = 6,   //* Transmission Control Protocol    
    IPPROTO_UDP  = 17,  //* User Datagram Protocol        
    IPPROTO_RAW  = 255, //* Raw IP packets
    IPPROTO_MAX
} EN_IPPROTO;

 //* ip帧头部结构体
PACKED_BEGIN
typedef struct _ST_IP_HDR_ {
    UCHAR bitHdrLen     : 4;    //* Length of the header,长度单位为4字节UINT型，不是单字节UCHAR型
    UCHAR bitVer        : 4;    //* version of IP
    UCHAR bitMustBeZero : 1;    //* Must be zero
    UCHAR bitTOS        : 4;    //* Type of service:（占4位从左至右为:最小时延、最大吞吐量、最高可靠性、最小费用，同时只能对1位置位，如4位均位0则表示一般服务）
    UCHAR bitPrior      : 3;    //* Ignore now
    USHORT usPacketLen;         //* Total length of the packet
    USHORT usID;                //* Unique identifier	
    UCHAR bitFragOffset0 : 5;   //* Frag offset0
    UCHAR bitFlag        : 3;   //* Flag	
    UCHAR bitFragOffset1 : 8;   //* Frag offset1
    UCHAR ubTTL;                //* Time to live
    UCHAR ubProto;              //* Protocol(TCP, UDP, etc)
    USHORT usChecksum;          //* IP checksum

    UINT unSrcIP;
    UINT unDstIP;
} PACKED ST_IP_HDR, *PST_IP_HDR;
PACKED_END

#if SUPPORT_IPV6
typedef struct _ST_IPV6_HDR_ {
	union {
		struct {
			UINT bitFlowLabel : 20; //* Flow Label，标识源和目的地址之间的通讯数据流，即表示当前传输的报文属于这个数据流中的一个，以显式地通知Ipv6路由器对其特
									//* 殊处理。其用于实现对数据包按照优先级顺序进行发送，比如优先发送实时数据（如音、视频）

			UINT bitEcn : 2;		//* Explicit Congestion Notification，显式拥塞通知-0：非ECN能力传输，非ECT即不支持拥塞通知；1、2：支持ECN传输；3：遇到拥塞
			UINT bitDscp : 6;		//* Differentiated Services Code Point，差分服务编码点，用于优先级指定，当网络出现拥塞时，低优先级的报文最先被丢弃	
			UINT bitVer : 4;		//* Ip版本号
		} stb32;
		UINT unVal;
	} uniFlag;

	USHORT usPayloadLen;	//* Payload Length，负载长度，Ipv6包携带的负载长度，即其携带的扩展头部和Icmpv6、Tcp、Udp等上层协议报文的长度
	UCHAR ubNextHdr;		//* Next Header，首个扩展头部或上层协议类型
	UCHAR ubHopLimit;		//* Hop Limit，跳数限制，表示Ipv6包能经过的最大链路数，超过的包则直接丢弃，路由器会发送Icmpv6超时消息到源主机（类似于Ipv4的ttl字段）

	UCHAR ubaSrcIp[16];		//* 源Ipv6地址 
	UCHAR ubaDstIp[16];		//* 目标Ipv6地址
} ST_IPV6_HDR, *PST_IPV6_HDR;

#define ipv6_ver uniFlag.stb32.bitVer
#define ipv6_dscp uniFlag.stb32.bitDscp
#define ipv6_ecn uniFlag.stb32.bitEcn
#define ipv6_flow_label uniFlag.stb32.bitFlowLabel
#define ipv6_flag uniFlag.unVal
#endif

#endif
