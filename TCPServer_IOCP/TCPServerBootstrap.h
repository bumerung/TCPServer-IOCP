#pragma once
#include "stdafx.h"
using namespace std;

class TCPServerBootstrap{
public:
	//���캯��
	TCPServerBootstrap() ;
	//�鹹����
	~TCPServerBootstrap();
	//��ʼ����
	bool StartListen(unsigned short port, string ip);
	void getConnectionClient();
	/*
	�ͷ�3�����ֲ��裺
	1. ���IOCP�̶߳��У��˳��߳�
	2. ��յȴ�accept�׽���m_vecAcps
	3. ����������׽���m_vecContInfo����ջ���
	*/
	void CloseServer();
	//˽�г�Ա����
private:
	//����CPU*2���̣߳��������������̸߳���
	int StartWorkThreadPool();
	//��ȡAcceptEx_�� GetAcceptExSockaddrs ����ָ��
	bool GetLPFNAcceptExAndGetAcceptSockaddrs();
	//����AcceptEx_����accept����
	bool PostAccept(COverlappedIOInfo* info);
	//����accept���� NumberOfBytes=0��ʾû���յ���һ֡���ݣ� >0ʱ��ʾ�յ��˵�һ֡����
	bool DoAccept(COverlappedIOInfo* info, DWORD NumberOfBytes = 0);
	//Ͷ��recv����
	bool PostRecv(COverlappedIOInfo* info);
	//����recv����
	bool DoRecv(COverlappedIOInfo* info);
	//�������ӵ�socket�б����Ƴ�socket���ͷſռ�
	bool DelectLink(SOCKET s);

	//˽�г�Ա����
private:
	//winsock�汾����
	WSAData m_wsaData;
	//�˿ڼ����׽���
	SOCKET m_sListen;
	//�ȴ�accept���׽��֣���Щ�׽��ֶ���û��ʹ�ù��ģ�����ΪACCEPT_SOCKET_NUM��ͬʱ����10���׽��ֵȴ�accept
	vector<SOCKET> m_vecAcps;
	//�ѽ������ӵ���Ϣ��ÿ���ṹ����һ���׽��֡����ͻ����������ջ��������Զ˵�ַ
	vector<COverlappedIOInfo*> m_vecContInfo;
	//����vector�Ļ��������
	mutex m_mu;
	//CIOCP��װ��
	CIOCP m_iocp;
	//AcceptEx_����ָ��
	LPFN_ACCEPTEX m_lpfnAcceptEx;
	//GetAcceptSockAddrs����ָ��
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptSockAddrs;

	//�����̳߳�
	std::vector<std::thread> workThreadPool_;
	//�����̳߳ش�С
	size_t size_;
};

typedef Singleton<TCPServerBootstrap> TCPServer;