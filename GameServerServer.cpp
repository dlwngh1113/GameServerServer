#define _CRT_SECURE_NO_WARNINGS
#include<iostream>
#include<WS2tcpip.h>
using namespace std;
#pragma comment (lib, "ws2_32.lib")

#define BUF_SIZE 1024
#define PORT 3500

char messageBuffer[BUF_SIZE];
SOCKET clientSocket;
WSABUF wsaBuf;

void error_display(const char* msg, int err_no);
void CALLBACK Send_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flasgs);
void CALLBACK Recv_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flasgs);

void error_display(const char* msg, int err_no)
{
	WCHAR* h_mess;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&h_mess, 0, NULL
	);
	cout << msg;
	wcout << L"  에러 => " << h_mess << endl;
	while (true);
	LocalFree(h_mess);
}

void CALLBACK Send_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flasgs)
{
	if (bytes > 0)
		cout << "TRACE - Send message : " << messageBuffer << "(" << bytes << "bytes)\n";
	else {
		closesocket(clientSocket);
		return;
	}
	wsaBuf.len = BUF_SIZE;
	ZeroMemory(&over, sizeof(*over));
	DWORD flag = NULL;
	int ret = WSARecv(clientSocket, &wsaBuf, 1, NULL, &flag, over, Recv_Complete);
}

void CALLBACK Recv_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flasgs)
{
	if (bytes > 0) {
		messageBuffer[bytes] = NULL;
		cout << "TRACE - Recv message : " << messageBuffer << "(" << bytes << "bytes)\n";
	}
	else {
		closesocket(clientSocket);
		return;
	}
	wsaBuf.len = bytes;
	ZeroMemory(&over, sizeof(*over));
	int ret = WSASend(clientSocket, &wsaBuf, 1, NULL, NULL, over, Send_Complete);
}

int main()
{
	WSADATA WSAdata;
	WSAStartup(MAKEWORD(2, 0), &WSAdata);
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	::bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
	listen(listenSocket, SOMAXCONN);

	POINT playerPt{ NULL,NULL };

	SOCKADDR_IN clientAddr;
	WSAOVERLAPPED overlapped;

	while (true) {
		int addSize = sizeof(clientAddr);
		clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &addSize);

		wsaBuf.buf = messageBuffer;
		wsaBuf.len = BUF_SIZE;
		DWORD flags = 0;
		ZeroMemory(&overlapped, sizeof(overlapped));
		int recvBytes = WSARecv(clientSocket, &wsaBuf, 1, NULL, &flags, &overlapped, Recv_Complete);

		while (true) {
			char buff[BUF_SIZE + 1];
			WSABUF wsabuf;
			wsabuf.buf = buff;
			wsabuf.len = BUF_SIZE;
			DWORD num_recv;
			DWORD flag = 0;
			WSARecv(clientSocket, &wsabuf, 1, &num_recv, &flag, NULL, NULL);
			cout << "Recieved " << num_recv << "Bytes [" << wsabuf.buf << "]\n";
			if (0 == num_recv)break;

			if (!strcmp(wsabuf.buf, "VK_LEFT"))
			{
				sprintf(wsabuf.buf, "%d", playerPt.x - 80);
			}
			else if (!strcmp(wsabuf.buf, "VK_RIGHT"))
			{
				sprintf(wsabuf.buf, "%d", playerPt.x + 80);
			}
			else if (!strcmp(wsabuf.buf, "VK_UP"))
			{
				sprintf(wsabuf.buf, "%d", playerPt.y - 80);
			}
			else if (!strcmp(wsabuf.buf, "VK_DOWN"))
			{
				sprintf(wsabuf.buf, "%d", playerPt.y + 80);
			}

			DWORD num_sent;
			wsabuf.len = strlen(wsabuf.buf);
			int ret = WSASend(clientSocket, &wsabuf, 1, &num_sent, 0, NULL, NULL);
			if (SOCKET_ERROR == ret) {
				error_display("WSASend", WSAGetLastError());
			}
			cout << "Sent " << wsabuf.len << "Bytes [" << buff << "]\n";
		}
		cout << "Client Connection Closed\n";
		closesocket(clientSocket);
	}
	closesocket(listenSocket);
	WSACleanup();
}