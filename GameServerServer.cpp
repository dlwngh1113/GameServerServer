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

void CALLBACK Send_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags);
void CALLBACK Recv_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags);

void CALLBACK Send_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	if (bytes > 0)
		cout << "TRACE - Send message : " << messageBuffer << "(" << bytes << "bytes)\n";
	else {
		cout << "Client Connection Closed\n";
		closesocket(clientSocket);
		return;
	}
	wsaBuf.len = BUF_SIZE;
	//ZeroMemory(&over, sizeof(*over));
	DWORD flag = NULL;
	int ret = WSARecv(clientSocket, &wsaBuf, 1, NULL, &flag, over, Recv_Complete);
}

void CALLBACK Recv_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	if (bytes > 0) {
		messageBuffer[bytes] = NULL;
		cout << "TRACE - Recv message : " << messageBuffer << "(" << bytes << "bytes)\n";
	}
	else {
		cout << "Client Connection Closed\n";
		closesocket(clientSocket);
		return;
	}
	wsaBuf.len = bytes;
	//ZeroMemory(&over, sizeof(*over));
	int ret = WSASend(clientSocket, &wsaBuf, 1, NULL, NULL, over, Send_Complete);
}

int main()
{
	WSADATA WSAdata;
	WSAStartup(MAKEWORD(2, 2), &WSAdata);
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN serverAddr;
	ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	::bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
	listen(listenSocket, SOMAXCONN);

	POINT playerPt{ NULL,NULL };

	SOCKADDR_IN clientAddr;
	WSAOVERLAPPED overlapped;

	while (true) {
		int addrSize = sizeof(clientAddr);
		clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &addrSize);

		wsaBuf.buf = messageBuffer;
		wsaBuf.len = BUF_SIZE;
		DWORD flags = 0;
		ZeroMemory(&overlapped, sizeof(overlapped));
		int recvBytes = WSARecv(clientSocket, &wsaBuf, 1, NULL, &flags, &overlapped, Recv_Complete);
	}
	closesocket(listenSocket);
	WSACleanup();
}