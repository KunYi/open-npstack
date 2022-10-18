# onps网络协议栈

#### 背景
大约是06年，因项目之需我开始接触应用于单片机系统的国外开源tcp/ip协议栈——LwIP，并借此顺势创作了我的第一本印成铅字的书——《嵌入式网络系统设计——基于Atmel ARM7系列》。这本书的反响还不错，好多人给我发msn（可惜这么好的一个即时通讯工具就这么被微软放弃了，好多联系人就此失联， :persevere: ）或邮件咨询相关问题。在我原来的写作计划中，这本书的出版只是一个开始，接下来还要写第二本——系统介绍LwIP包含的ppp协议栈的移植、应用及设计实现等相关内容。但，事与愿违，这本书跳票了，且这一跳就是十二年……

细细想来，当初跳票的主因有二：其一，因家庭、工作等致可支配时间太少；其二，缺乏足够的ppp协议相关知识及技术储备致信心不足，畏首畏尾，裹足不前。但，这件事始终是我的一个遗憾。十二年的时间，不长亦不短，但足够让心底的遗憾变成一粒小小的种子并茁壮成长为一棵梦想的参天大树。

如今，世界来到了疫情肆虐的二零年代。我的可支配时间多了起来，技术能力亦远非当年可比。梦想之树到了开花结果的时候了。遥想当初，入行还没几年，技术能力有限，我只能站在大神的肩膀上研究如何移植、使用LwIP，ppp栈碰都没敢碰。现在，如果还只是延续十几年前的工作，那这件事做起来就无甚意义。基于对自身技术实力的准确认识，我决定自己从零开始搭建一个完整的网络协议栈。终，历6个月余，onps协议栈（onps，open net protocol stack）完成初版开发，并内部测试通过。十余年的遗憾今日得偿。另，从业20余年，内心终有一个做核心基础软件的梦。今，这二之梦想亦借此得偿。

新莺初啼，总免不了会有诸多不尽如人意的地方。开源，则可与志趣相投者共享、共用、共研，历诸位严苛手段使之快速迭代，快速成熟，比肩LwIP可期 :blush: 。


#### 简介
onps是一个开源且完全自主开发的国产网络协议栈，适用于资源受限的单片机系统，提供完整地ethernet/ppp/tcp/ip协议族实现，同时提供sntp、dns、ping等网络工具，支持以太网环境下dhcp动态ip地址申请，也支持动态及静态路由表。协议栈还封装实现了一个伯克利套接字（Berkeley sockets）层。该层并没有完全按照Berkeley sockets标准设计实现，而是我根据以往socket编程经验，以方便用户使用、简化用户编码为设计目标，重新声明并定义了一组常见socket接口函数：
- socket：创建一个socket，目前仅支持udp和tcp两种类型
- close：关闭一个socket，释放当前占用的协议栈资源
- connect：与目标tcp服务器建立连接（阻塞型）或绑定一个固定的udp服务器地址
- connect_nb：与目标tcp服务器建立连接（非阻塞型）
- is_tcp_connected：获取当前tcp链路的连接状态
- send：数据发送函数，tcp链路下为阻塞型
- send_nb：数据发送函数，非阻塞型
- is_tcp_send_ok：数据是否已成功送达tcp链路的对端（收到tcp ack报文）
- sendto：udp数据发送函数，发送数据到指定目标地址
- recv：数据接收函数，udp/tcp链路通用
- recvfrom：数据接收函数，用于udp链路，接收数据的同时函数会返回数据源的地址信息
- socket_set_rcv_timeout：设定recv()函数接收等待的时长，单位：秒
- bind：绑定一个固定端口、地址
- listen：tcp服务器进入监听状态
- accept：接受一个到达的tcp连接请求
- tcpsrv_recv_poll：tcp服务器专用函数，等待任意一个或多个tcp客户端数据到达信号
- socket_get_last_error：获取socket最近一次发生的错误信息
- socket_get_last_error_code：获取socket最近一次发生的错误编码

协议栈简化了传统BSD socket编程需要的一些繁琐操作，将一些不必要的操作细节改为底层实现，比如select/poll模型、阻塞及非阻塞读写操作等。简化并不意味着推翻，socket接口函数的基本定义、主要参数、使用方法并没有改变，你完全可以根据以往经验及编程习惯快速上手并熟练使用onps栈sockets。 **无须过多关注协议栈底层，利用socket api编程即可完全满足复杂通讯应用的需求，而不像LwIp一样需要使用它自定义的一组接口函数才能达成同样的目标。** 

为了适应单片机系统对内存使用极度变态的苛刻要求，onps协议栈在设计之初即考虑采用写时零复制（zero copy）技术。用户层数据在向下层协议传递过程中，协议栈采用buf list链表技术将它们链接到一起，直至将其发送出去，均无须任何内存复制操作。另外，协议栈采用buddy算法提供安全、可靠的动态内存管理功能，以期最大限度地提高协议栈运行过程中的内存利用率并尽可能地减少内存碎片。

不同于本世纪00到10年代初，单片机的应用场景中ucosii等rtos尚未大规模普及，前后台系统还大行其道的时代，现如今大部分的应用场景下开发人员选择使用rtos已成为主流。因此，协议栈在设计之初即不支持前后台模式，其架构设计建立在时下流行的rtos（RT-Thread、ucosii/iii等）之上。协议栈移植的主要工作也就自然是针对不同rtos编写相关os适配层功能函数了。当然，如果你有着极其特定的应用场景，需要将onps栈移植到采用前后台模式的单片机上，我的建议是保留tcp/udp之下协议层的通讯处理逻辑，调整上层的系统架构使其适应目标系统运行模式。

#### 软件架构
onps栈设计实现了一套完整的tcp/ip协议模型。从数据链路层到ip层，再到tcp/udp层以及之上的伯克利socket层，最后是用户自己的通讯应用层，onps栈实现了全栈覆盖，能够满足绝大部分的网络编程需求。其架构如下：
![输入图片说明](onps%E6%A0%88%E6%9E%B6%E6%9E%84%E5%9B%BE.jpg)

可以看出，其与传统的网络编程模型并没有什么不同，用户仍然是继续利用socket api编写常见的tcp及udp网络应用。同时你还可以利用协议栈提供的几个网络工具进行网络校时、dns查询等操作。

#### 目录结构

| 名称  | 描述  |
|---|---|
| bsd  | 伯克利sockets层  |
|   |   |
|   |   |
|   |   |
|   |   |
|   |   |
|   |   |
|   |   |
|   |   |


#### 移植及使用说明

移植的核心工作就是完成RTOS模拟层的编写及适配，详细的移植说明请参考《onps网络协议栈移植及使用说明v1.0》一文，点此[下载](https://gitee.com/Neo-T/open-npstack/releases/download/v1.0.0.221017/onps%E7%BD%91%E7%BB%9C%E5%8D%8F%E8%AE%AE%E6%A0%88%E7%A7%BB%E6%A4%8D%E5%8F%8A%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8Ev1.0.7z)。

协议栈支持主流的ARM Cortex系列MCU，支持Keil MDK、IAR等常见IDE。

#### 参与贡献

1.  Fork 本仓库
2.  新建 Feat_xxx 分支
3.  提交代码
4.  新建 Pull Request


#### 特技

1.  使用 Readme\_XXX.md 来支持不同的语言，例如 Readme\_en.md, Readme\_zh.md
2.  Gitee 官方博客 [blog.gitee.com](https://blog.gitee.com)
3.  你可以 [https://gitee.com/explore](https://gitee.com/explore) 这个地址来了解 Gitee 上的优秀开源项目
4.  [GVP](https://gitee.com/gvp) 全称是 Gitee 最有价值开源项目，是综合评定出的优秀开源项目
5.  Gitee 官方提供的使用手册 [https://gitee.com/help](https://gitee.com/help)
6.  Gitee 封面人物是一档用来展示 Gitee 会员风采的栏目 [https://gitee.com/gitee-stars/](https://gitee.com/gitee-stars/)
