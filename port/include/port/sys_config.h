/* sys_config.h
 *
 * 系统配置头文件，用户可根据实际情况对协议栈进行裁剪、参数配置等工作
 *
 * Neo-T, 创建于2022.03.11 14:45
 * 版本: 1.0
 *
 */
#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

#define NETIF_NUM               2       //* 系统支持的网卡数量
#define SOCKET_NUM_MAX          10      //* 系统支持的最大SOCKET数量，如实际应用中超过这个数量则会导致用户层业务逻辑无法全部正常运行（icmp/tcp/udp业务均受此影响）
#define IP_TTL_DEFAULT          64      //* 缺省TTL值

 //* 系统支持哪些功能模块由此配置
 //* ===============================================================================================
#define SUPPORT_PRINTF	1	//* 是否支持调用printf()输出相关调试或系统信息
#if SUPPORT_PRINTF
    #define PRINTF_THREAD_MUTEX 1   //* 是否支持使用printf线程互斥锁，确保不同线程的调试输出信息不被互相干扰，值为1则支持互斥锁
	#define DEBUG_LEVEL         1	//* 调试级别为0的话，则不输出原始报文，为1则输出
#endif

#define SUPPORT_PPP		1	//* 是否支持ppp模块：1，支持；0，不支持，如果选择支持，则系统会将ppp模块代码加入到协议栈中
#if SUPPORT_PPP
	#define APN_DEFAULT				"4gnet"		//* 根据实际情况在这里设置缺省APN
	#define AUTH_USER_DEFAULT		"card"		//* ppp认证缺省用户名
	#define AUTH_PASSWORD_DEFAULT	"any_char"	//* ppp认证缺省口令

	#define PPP_NETLINK_NUM			1	//* 最多支持几路ppp链路（系统存在几个modem这里就指定几就行）
	#define	SUPPORT_ECHO			1	//* 对端是否支持echo链路探测
	#define WAIT_ACK_TIMEOUT_NUM	5	//* 在这里指定连续几次接收不到对端的应答报文就进入协议栈故障处理流程（STACKFAULT），这意味着当前链路已经因严重故障终止了
#endif

#define SUPPORT_ETHERNET    1   //* 是否支持ethernet：1，支持；0，不支持

#define SUPPORT_IPV6	0	//* 是否支持IPv6：1，支持；0，不支持
 //* ===============================================================================================

//* 内存管理单元(mmu)相关配置项，其直接影响协议栈能分配多少个socket给用户使用
//* ===============================================================================================
#define BUDDY_PAGE_SIZE  32     //* 系统能够分配的最小页面大小，其值必须是2的整数次幂
#define BUDDY_ARER_COUNT 9      //* 指定buddy算法管理的内存块数组单元数量

#define BUDDY_MEM_SIZE   8192   //* buddy算法管理的内存总大小，其值由BUDDY_PAGE_SIZE、BUDDY_ARER_COUNT两个宏计算得到：
                                //* 32 * (2 ^ (9 - 1))，即BUDDY_MEM_SIZE = BUDDY_PAGE_SIZE * (2 ^ (BUDDY_ARER_COUNT - 1))
								//* 之所以在此定义好要管理的内存大小，原因是buddy管理的内存其实就是一块提前分配好的静态存储
								//* 时期的字节型一维数组，以确保协议栈不占用宝贵的堆空间
//* ===============================================================================================

//* ip支持的上层协议相关配置项
//* ===============================================================================================
#define ICMPRCVBUF_SIZE_DEFAULT 128     //* icmp发送echo请求报文时指定的接收缓冲区的缺省大小，注意，如果要发送较大的ping包就必须指定较大的接收缓冲区

#define TCPRCVBUF_SIZE_DEFAULT  2048    //* tcp层缺省的接收缓冲区大小，大小应是2^n次幂才能最大限度不浪费budyy模块分配的内存

#define TCPUDP_PORT_START       20000   //* TCP/UDP协议动态分配的起始端口号

#define TCP_WINDOW_SCALE        0       //* 窗口扩大因子缺省值
#define TCP_CONN_TIMEOUT        30      //* 缺省TCP连接超时时间
#define TCP_ACK_TIMEOUT         3       //* 缺省TCP应答超时时间
#define TCP_MSL                 15      //* 指定TCP链路TIMEWAIT态的最大关闭时长：2 * TCP_MSL，单位：秒
#define TCP_LINK_NUM_MAX        16      //* 系统支持最多建立多少路TCP链路（TCP客户端 + TCP服务器最大并发连接数）
#define UDP_LINK_NUM_MAX        4       //* 调用connect()函数连接对端udp服务器的最大数量（一旦调用connect()函数，收到的非服务器报文将被直接丢弃）
#define SUPPORT_SACK            0       //* 系统是否支持sack项，sack项需要协议栈建立发送队列，这个非常消耗内存，通用版本不支持该项
//* ===============================================================================================

#endif