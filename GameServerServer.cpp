#define _CRT_SECURE_NO_WARNINGS
#include<iostream>
#include<string>
#include<map>
#include<WS2tcpip.h>
using namespace std;
#pragma comment (lib, "ws2_32.lib")

#define BUF_SIZE 1024
#define PORT 3500
#define SECTOR_SIZE 80

struct King {
	char id;
	long x;
	long y;
};

struct Client {
	King king;

	SOCKET socket;

	char sendBuffer[BUF_SIZE + 1];
	char recvBuffer[BUF_SIZE + 1];
	WSABUF sendWsabuf;
	WSABUF recvWsabuf;
	WSAOVERLAPPED sendOver;
	WSAOVERLAPPED recvOver;
};

map<SOCKET, Client> clients;

void CALLBACK Send_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags);
void CALLBACK Recv_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags);

void CALLBACK Send_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	SOCKET target = reinterpret_cast<SOCKET>(over->hEvent);

	if (bytes > 0) {
		cout << "TRACE - Send message : " << clients[target].sendBuffer << "(" << bytes << "bytes)\n";
	}
	else {
		cout << "Send_Complete error : Client Connection Closed\n";
		closesocket(clients[target].socket);
		clients.erase(target);
		return;
	}
	clients[target].recvWsabuf.len = BUF_SIZE;
	ZeroMemory(over, sizeof(*over));
	over->hEvent = (HANDLE)target;
	DWORD flag = 0;
	WSARecv(clients[target].socket, &clients[target].recvWsabuf, 1, NULL, &flag, over, Recv_Complete);
}

void CALLBACK Recv_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	SOCKET target = reinterpret_cast<SOCKET>(over->hEvent);

	if (bytes > 0) {
		cout << "TRACE - Recv message : " << atoi(clients[target].recvBuffer) << "(" << bytes << "bytes)\n";
		string s{};
		WPARAM wParam = atoi(clients[target].recvBuffer);
		switch (wParam)
		{
		case VK_LEFT:
			if (clients[target].king.x - SECTOR_SIZE >= 0)
				clients[target].king.x -= 80;
			s += to_string(clients[target].king.id) + " " + to_string(clients[target].king.x) + " " + to_string(clients[target].king.y);
			break;
		case VK_RIGHT:
			if (clients[target].king.x + SECTOR_SIZE <= SECTOR_SIZE * 8)
				clients[target].king.x += 80;
			s += to_string(clients[target].king.id) + " " + to_string(clients[target].king.x) + " " + to_string(clients[target].king.y);
			break;
		case VK_UP:
			if (clients[target].king.y - SECTOR_SIZE >= 0)
				clients[target].king.y -= 80;
			s += to_string(clients[target].king.id) + " " + to_string(clients[target].king.x) + " " + to_string(clients[target].king.y);
			break; 
		case VK_DOWN:
			if (clients[target].king.y + SECTOR_SIZE <= SECTOR_SIZE * 8)
				clients[target].king.y += 80;
			s += to_string(clients[target].king.id) + " " + to_string(clients[target].king.x) + " " + to_string(clients[target].king.y);
			break;
		}
		strcpy_s(clients[target].sendBuffer, s.c_str());
		clients[target].sendWsabuf.buf = clients[target].sendBuffer;
	}
	else {
		cout << "Recv_Complete error : Client Connection Closed\n";
		closesocket(clients[target].socket);
		clients.erase(target);
		return;
	}
	for (auto& p : clients){
		ZeroMemory(&p.second.sendOver, sizeof(p.second.sendOver));
		p.second.sendOver.hEvent = reinterpret_cast<HANDLE>(p.second.socket);
		WSASend(p.second.socket, &p.second.sendWsabuf, 1, NULL, NULL, &p.second.sendOver, Send_Complete);
	}
	//WSASend(clients[target].socket, &clients[target].wsaBuf, 1, NULL, NULL, over, Send_Complete);
}

int main()
{
	WSADATA WSAdata;
	if(WSAStartup(MAKEWORD(2, 2), &WSAdata) != 0)
		return 1;
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN serverAddr;
	ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	::bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
	listen(listenSocket, SOMAXCONN);
	char kingId = 0;

	//client socket init
	while (true) {
		SOCKADDR_IN addr{};
		int addrsize = sizeof(addr);
		SOCKET id = WSAAccept(listenSocket, (sockaddr*)&addr, &addrsize, NULL, NULL);

		//clients socket process
		clients[id] = Client{};
		clients[id].socket = id;
		clients[id].king.x = clients[id].king.y = 0;
		clients[id].sendWsabuf.buf = clients[id].sendBuffer;
		clients[id].recvWsabuf.buf = clients[id].recvBuffer;
		clients[id].sendWsabuf.len = BUF_SIZE;
		clients[id].recvWsabuf.len = BUF_SIZE;
		ZeroMemory(&clients[id].sendOver, sizeof(clients[id].sendOver));
		ZeroMemory(&clients[id].recvOver, sizeof(clients[id].recvOver));
		clients[id].recvOver.hEvent = (HANDLE)id;
		clients[id].king.id = kingId++;
		//클라이언트 세팅
		DWORD flags = 0;
		//클라이언트에서 보내주는 데이터 대기
		int recvBytes = WSARecv(clients[id].socket, &clients[id].recvWsabuf, 1, NULL, &flags, &clients[id].recvOver, Recv_Complete);
	}
	closesocket(listenSocket);
	WSACleanup();
}