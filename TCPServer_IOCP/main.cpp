#include "stdafx.h"
#include "TCPServerBootstrap.h"

int main(int argc, char *argv[])
{
	TCPServerBootstrap* server = TCPServer::getInstance();
	server->StartListen(20000, "0.0.0.0");
	int cmd = 0;
	while (cin >> cmd)
	{
		if (cmd == 1)
		{
			//����1  �ر�TCP������
			server->CloseServer();
			break;
		}
		else
		{
			//�����������֣���ӡ�������߿ͻ�����Ϣ
			server->getConnectionClient();
		}
	}
	Sleep(5000);
	return 0;
}
