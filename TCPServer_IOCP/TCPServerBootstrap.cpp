#include "stdafx.h"
#include "TCPServerBootstrap.h"
/*构造函数*/

TCPServerBootstrap::TCPServerBootstrap()
	:m_lpfnAcceptEx(NULL), m_lpfnGetAcceptSockAddrs(NULL),size_(0)
{
	WSAStartup(MAKEWORD(2,2), &m_wsaData);
}

/*析构函数*/
TCPServerBootstrap::~TCPServerBootstrap()
{
	CloseServer();
	WSACleanup();
}

/*TCP Server开启监听*/
bool TCPServerBootstrap::StartListen(unsigned short port, string ip)
{
	//listen socket需要将accept操作投递到完成端口，因此listen socket属性必须有重叠IO
	m_sListen = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (m_sListen == INVALID_SOCKET)
	{
		cerr << "WSASocket create socket error!!!" << endl;
		return false;
	}
	cout<< "WSASocket create socket success" << endl;
	//创建并设置IOCP并发线程数量
	if (m_iocp.Create() == false)
	{
		cerr << "IOCP create error, error code=" << WSAGetLastError << endl;
		return false;
	}
	cout<< "IOCP create success" << endl;
	//将listen socket绑定至IOCP
	if (!m_iocp.AssociateSocket(m_sListen, TYPE_ACP))
	{
		cerr << "IOCP associate listen socket error!!!" << endl;
		return false;
	}
	cout << "IOCP associate listen socket success" << endl;
	//创建Socket
	sockaddr_in service;
	service.sin_family = AF_INET;
	service.sin_port = htons(port);
	if ( ip.empty() )
	{
		service.sin_addr.s_addr = INADDR_ANY;
	}
	else
	{
		service.sin_addr.s_addr = inet_addr(ip.c_str());
	}
	if ( ::bind(m_sListen, (sockaddr*)&service, sizeof(service)) == SOCKET_ERROR)
	{
		cerr << "Socket bind error! error code=" << WSAGetLastError << endl;
		return false;
	}
	cout << "Socket bind success!!!" << endl;
	if (listen(m_sListen, SOMAXCONN) == SOCKET_ERROR)
	{
		cerr << "Socket listen error! error code=" << WSAGetLastError << endl;
	}
	cout << "Socket listen success!!!" << endl;
	//启动工作者线程
	size_t threadnum = StartWorkThreadPool();
	cout << "Start work thread pool, thread num =" << threadnum << endl;
	//获取AcceptEx_和GetAcceptSockAddrs函数指针
	if (!GetLPFNAcceptExAndGetAcceptSockaddrs())
	{
		cerr << "GetLPFNAcceptExAndGetAcceptSockaddrs error!!!" << endl;
		return false;
	}
	//cout << "GetLPFNAcceptExAndGetAcceptSockaddrs Success!!!" << endl;
	//创建10个AcceptEx
	for (int i = 0; i < 10; i++)
	{
		COverlappedIOInfo* info = new COverlappedIOInfo();
		if (!PostAccept(info))
		{
			delete info;
			return false;
		}
	}
	return true;
}

/*获取所有在线的客户端信息，并打印*/
void TCPServerBootstrap::getConnectionClient()
{
	cout << "Connection Client List: " << endl;
	for_each(m_vecContInfo.begin(), m_vecContInfo.end(),
					[](COverlappedIOInfo* olinfo) 
					{
							cout << "        Client ip=" << inet_ntoa(olinfo->m_addr.sin_addr) << ":" << olinfo->m_addr.sin_port << endl;
					});
}


/*启动工作者线程池*/
size_t TCPServerBootstrap::StartWorkThreadPool()
{
	if (size_ == 0)
	{
		size_ = std::thread::hardware_concurrency();
	}
	for (int i = 0; i < size_; i++)
	{
		workThreadPool_.emplace_back(
			[this]() 
			{
				//std::cout << "work thread id=" << std::this_thread::get_id <<std::endl;
				while (true)
				{
					DWORD NumberOfBytes = 0;
					ULONG_PTR completionKey = NULL;
					OVERLAPPED* ol = NULL;
					//阻塞调用GetQueuedCompletionStatus获取完成端口事件
					//GetQueuedCompletionStatus成功返回
					if ( GetQueuedCompletionStatus(this->m_iocp.GetIOCP(), &NumberOfBytes, &completionKey, &ol, WSA_INFINITE) )
					{
						COverlappedIOInfo* olinfo = (COverlappedIOInfo*)ol;
						if (completionKey == TYPE_CLOSE)
						{
							break;
						}
						//客户端断开连接
						if (NumberOfBytes == 0 && (completionKey == TYPE_RECV || completionKey == TYPE_SEND))
						{
							std::cout << "Client ip=" << inet_ntoa(olinfo->m_addr.sin_addr) << ":" << olinfo->m_addr.sin_port << " closed!!!" << endl;
							this->DelectLink(olinfo->m_socket);
							continue;
						}
						switch (completionKey)
						{
							//客户端接入完成事件句柄
							case TYPE_ACP:
							{
								this->DoAccept(olinfo, NumberOfBytes);
								this->PostAccept(olinfo);
							}break;
							//数据接收完成事件句柄
							case TYPE_RECV:
							{
								this->DoRecv(olinfo);
								this->PostRecv(olinfo);
							}break;
							//数据发送完成事件句柄
							case TYPE_SEND:
							{
							}break;
							//默认处理句柄
							default:
								break;
						}
					}
					//GetQueuedCompletionStatus出错
					else
					{
						int res = WSAGetLastError();
						switch (res)
						{
							case ERROR_NETNAME_DELETED:
							{
								COverlappedIOInfo* olinfo = (COverlappedIOInfo*)ol;
								if (olinfo)
								{
									cerr << "Client ip=" << inet_ntoa(olinfo->m_addr.sin_addr) << ":" << olinfo->m_addr.sin_port << "exception exit!!!" << endl;
									this->DelectLink(olinfo->m_socket);
								}
							}break;
							default:
							{
								cerr << "work thread GetQueuedCompletionStatus error, error code=" << WSAGetLastError() << endl;
							}break;
						}
					}
				}
				cout << "work thread id=" << std::this_thread::get_id << " stoped!!!" << endl;
			});
	}
	return size_;
}

/*GetLPFNAcceptExAndGetAcceptSockaddrs 获取相应函数指针*/
bool TCPServerBootstrap::GetLPFNAcceptExAndGetAcceptSockaddrs()
{
	DWORD ByteReturned = 0;
	//获取AcceptEx函数指针
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	if (SOCKET_ERROR == WSAIoctl(
																	m_sListen,
																	SIO_GET_EXTENSION_FUNCTION_POINTER,
																	&GuidAcceptEx,
																	sizeof(GuidAcceptEx),
																	&m_lpfnAcceptEx,
																	sizeof(m_lpfnAcceptEx),
																	&ByteReturned,
																	NULL, NULL
																	))
	{
		cerr << "WSAIoctl get AcceptEx function error, error code=" << WSAGetLastError() << endl;
		return false;
	}
	//获取GetAcceptExSockAddrs函数指针
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
	if (SOCKET_ERROR == WSAIoctl(
																		m_sListen,
																		SIO_GET_EXTENSION_FUNCTION_POINTER,
																		&GuidGetAcceptExSockAddrs,
																		sizeof(GuidGetAcceptExSockAddrs),
																		&m_lpfnGetAcceptSockAddrs,
																		sizeof(m_lpfnGetAcceptSockAddrs),
																		&ByteReturned,
																		NULL, NULL
																	))
	{
		cerr << "WSAIoctl get GetAcceptExSockAddrs function error, error code=" << WSAGetLastError() << endl;
		return false;
	}
	return true;
}

/*投递AcceptEx事件*/
bool TCPServerBootstrap::PostAccept(COverlappedIOInfo * info)
{
	if (m_lpfnAcceptEx == NULL)
	{
		cerr << "m_lpfnAcceptEx is NULL!!!" << endl;
		return false;
	}
	SOCKET socket = info->m_socket;
	info->ResetRecvBuffer();
	info->ResetSendBuffer();
	info->ResetOverlapped();
	info->m_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (info->m_socket == INVALID_SOCKET)
	{
		cerr << "WSASocket error, error code=" << WSAGetLastError() << endl;
		return false;
	}
	/*
	这里建立的socket用来和对端建立连接，最终会被加入m_vecContInfo列表中
	调用AcceptEx将accept socket绑定至完成端口， 并开始进行事件监听
	这里需要传递Overlapped， new一个COverlappedIOInfo
	AcceptEx是m_listen的监听事件， m_listen已经绑定了完成端口；虽然info->m_socket已经创建，但是未使用，
	现在不必为info->m_socket绑定完成端口， 在AcceptEx事件发生后，再为info->m_socket绑定IOCP
	*/
	DWORD byteReceived = 0;
	if (false == m_lpfnAcceptEx
							(
								m_sListen,
								info->m_socket,
								info->m_recvBuf.buf,
								info->m_recvBuf.len - (sizeof(SOCKADDR_IN) + 16) * 2,
								sizeof(SOCKADDR_IN) + 16,
								sizeof(SOCKADDR_IN) + 16,
								&byteReceived,
								info
							))
	{
		DWORD res = WSAGetLastError();
		if (ERROR_IO_PENDING != res)
		{
			cerr << "AcceptEx error, error code=" << res << endl;
			return false;
		}
	}
	std::vector<SOCKET>::iterator iter = m_vecAcps.begin();
	for ( ; iter != m_vecAcps.end(); ++iter)
	{
		if (*iter == socket)
		{
			*iter = info->m_socket;
		}
	}
	//列表中没有找到socket
	if (iter == m_vecAcps.end())
	{
		m_vecAcps.push_back(info->m_socket);
	}
	return true;
}

/*有客户的接入，客户端接入事件处理句柄*/
bool TCPServerBootstrap::DoAccept(COverlappedIOInfo * info, DWORD NumberOfBytes)
{
	/*
	分支用于获取远端地址
	如果接收TYPE_ACP的同时，接收到了第一帧数据，则第一帧数据中包含远端地址
	如果没有收到第一帧数据，则通过getpeername获取远端地址
	*/
	SOCKADDR_IN* ClientAddr = NULL;
	int remoteLen = sizeof(SOCKADDR_IN);
	if (NumberOfBytes > 0)
	{
		/*接收的数据分为3部分：1. 客户端发来的数据  2. 本地地址  3. 远端地址*/
		if (m_lpfnGetAcceptSockAddrs)
		{
			SOCKADDR_IN* LocalAddr = NULL;
			int localLen = sizeof(SOCKADDR_IN);
			m_lpfnGetAcceptSockAddrs(
																		info->m_recvBuf.buf,
																		info->m_recvBuf.len - (sizeof(SOCKADDR_IN) + 16) * 2,
																		sizeof(SOCKADDR_IN) + 16,
																		sizeof(SOCKADDR_IN) + 16,
																		(LPSOCKADDR*) &LocalAddr,
																		&localLen,
																		(LPSOCKADDR*)&ClientAddr,
																		&remoteLen
																	);
			cout << "new client connection from ip=" << inet_ntoa(ClientAddr->sin_addr) << ":" << ClientAddr->sin_port
				<< " Current Thread id=" << GetCurrentThreadId
				<< endl << "Recved Data: " << info->m_recvBuf.buf << endl;
		}
		//未收到第一帧数据
		else if (NumberOfBytes == 0)
		{
			cout << "New Connection!!!" << endl;
			if (SOCKET_ERROR == getpeername(info->m_socket, (sockaddr*)ClientAddr, &remoteLen))
			{
				cerr << "getpeername error, error code=" << WSAGetLastError() << endl;
			}
			else
			{
				cout << "new client connection from ip=" << inet_ntoa(ClientAddr->sin_addr) << ":" << ClientAddr->sin_port << endl;
			}
		}

		COverlappedIOInfo* olinfo = new COverlappedIOInfo();
		olinfo->m_socket = info->m_socket;
		olinfo->m_addr = *ClientAddr;
		//服务端只收取recv，同时监听recv和send可用设计位偏移， 用或运算实现
		if (m_iocp.AssociateSocket(olinfo->m_socket, TYPE_RECV))
		{
			PostRecv(olinfo);
			m_vecContInfo.push_back(olinfo);
		}
		else
		{
			delete olinfo;
			return false;
		}
	}
	return true;
}

/*投递一个接收数据完成端口*/
bool TCPServerBootstrap::PostRecv(COverlappedIOInfo * info)
{
	DWORD BytesRecevd = 0;
	DWORD dwFlags = 0;
	info->ResetOverlapped();
	info->ResetRecvBuffer();
	int recvNum = WSARecv(info->m_socket, &info->m_recvBuf, 1 , &BytesRecevd, &dwFlags, (OVERLAPPED*)info, NULL);
	if (recvNum != 0)
	{
		int res = WSAGetLastError();
		if (WSA_IO_PENDING != res)
		{
			cerr << "WSARecv error, error code=" << res << endl;
		}
	}
	return true;
}

/*接收数据处理句柄*/
bool TCPServerBootstrap::DoRecv(COverlappedIOInfo * info)
{
	cout << "Received data from ip=" << inet_ntoa(info->m_addr.sin_addr) << ":" << info->m_addr.sin_port 
		<< " Current Thread id="<<GetCurrentThreadId
		<< endl<< "Recved Data: " << info->m_recvBuf.buf << endl;
	/*回声服务测试 echo service*/
	//投递一个发送数据完成端口

	return true;
}

/*删除失效连接句柄*/
bool TCPServerBootstrap::DelectLink(SOCKET s)
{
	std::lock_guard<std::mutex> lock(m_mu);
	std::vector<COverlappedIOInfo*>::iterator iter = m_vecContInfo.begin();
	bool found = false;   //标志位，客户端在列表中找到与否
	for (; iter != m_vecContInfo.end(); ++iter)
	{
		if (s == (*iter)->m_socket)
		{
			COverlappedIOInfo* ol = *iter;
			closesocket(s);
			m_vecContInfo.erase(iter);
			delete ol;
			found = true;   //找到了
			break;
		}
	}
	if (!found)  //客户端在列表中没有找到
	{
		cerr << "socket is not in m_vecContInfo" << endl;
		return false;
	}
	else return true;
}

/*关闭服务器*/
void TCPServerBootstrap::CloseServer()
{
	//1. 清空IOCP线程队列， 退出工作线程， 给所有的线程发送PostQueueCompletionStatus信息
	for (int i = 0; i < size_; i++)
	{
		if (false == m_iocp.PostStatus(TYPE_CLOSE))
		{
			cerr << "PostQueuedCompletionStatus error, error code=" << WSAGetLastError() << endl;
		}
	}
	cout << "Cleaned IOCP work thread" << endl;
	//2. 清空等待accept的套接字m_vecAcps
	std::vector<SOCKET>::iterator iter = m_vecAcps.begin();
	for (; iter != m_vecAcps.end(); ++iter)
	{
		closesocket(*iter);
	}
	m_vecAcps.clear();
	cout << "Cleaned m_vecAcps" << endl;
	//3. 清空已连接的套接字m_vecContInfo并清空缓存
	for_each(m_vecContInfo.begin(), m_vecContInfo.end(), 
					[](COverlappedIOInfo* olinfo) 
					{
						closesocket(olinfo->m_socket);
						delete olinfo;
					});
	m_vecContInfo.clear();
	cout << "Cleaned m_vecContInfo" << endl;
}
