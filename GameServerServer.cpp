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

struct Client {
	short x, y;

	SOCKET socket;
	char messageBuffer[BUF_SIZE + 1];
	WSABUF wsaBuf;
	WSAOVERLAPPED overlapped;
};

map<SOCKET, Client> clients;

void CALLBACK Send_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags);
void CALLBACK Recv_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags);

void CALLBACK Send_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	SOCKET target = reinterpret_cast<SOCKET>(over->hEvent);

	if (bytes > 0) {
		string s;
		int idx = 0;
		if (!strcmp(clients[target].messageBuffer, "VK_LEFT")) {
			if(clients[target].x - SECTOR_SIZE >= 0)
				clients[target].x -= 80;
			s += to_string(idx) + " " + to_string(clients[target].x) + " " + to_string(clients[target].y);
		}
		else if (!strcmp(clients[target].messageBuffer, "VK_RIGHT")) {
			if (clients[target].x + SECTOR_SIZE <= SECTOR_SIZE * 8)
				clients[target].x += 80;
			s += to_string(idx) + " " + to_string(clients[target].x) + " " + to_string(clients[target].y);
		}
		else if (!strcmp(clients[target].messageBuffer, "VK_UP")) {
			if (clients[target].y - SECTOR_SIZE >= 0)
				clients[target].y -= 80;
			s += to_string(idx) + " " + to_string(clients[target].x) + " " + to_string(clients[target].y);
		}
		else if (!strcmp(clients[target].messageBuffer, "VK_DOWN")) {
			if (clients[target].y - SECTOR_SIZE <= SECTOR_SIZE * 8)
				clients[target].y += 80;
			s += to_string(idx) + " " + to_string(clients[target].x) + " " + to_string(clients[target].y);
		}
		for (auto& client : clients) {
			if (client.first != target) {
				++idx;
				s += " " + to_string(idx) + " " + to_string(clients[target].x) + " " + to_string(clients[target].y);
			}
		}
		strcpy_s(clients[target].messageBuffer, s.c_str());
		cout << "TRACE - Send message : " << clients[target].messageBuffer << "(" << bytes << "bytes)\n";
	}
	else {
		cout << "Client Connection Closed\n";
		closesocket(clients[target].socket);
		clients.erase(target);
		return;
	}
	clients[target].wsaBuf.len = BUF_SIZE;
	ZeroMemory(over, sizeof(*over));
	clients[target].overlapped.hEvent = (HANDLE)target;
	DWORD flag = 0;
	int ret = WSARecv(clients[target].socket, &clients[target].wsaBuf, 1, NULL, &flag, &clients[target].overlapped, Recv_Complete);
}

void CALLBACK Recv_Complete(DWORD err, DWORD bytes, LPWSAOVERLAPPED over, DWORD flags)
{
	SOCKET target = reinterpret_cast<SOCKET>(over->hEvent);

	if (bytes > 0) {
		clients[target].messageBuffer[bytes] = NULL;
		cout << "TRACE - Recv message : " << clients[target].messageBuffer << "(" << bytes << "bytes)\n";
	}
	else {
		cout << "Client Connection Closed\n";
		closesocket(clients[target].socket);
		clients.erase(target);
		return;
	}
	clients[target].wsaBuf.len = bytes;
	ZeroMemory(over, sizeof(*over));
	clients[target].overlapped.hEvent = (HANDLE)target;
	int ret = WSASend(clients[target].socket, &clients[target].wsaBuf, 1, NULL, NULL, &clients[target].overlapped, Send_Complete);
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

	//client socket init
	while (true) {
		SOCKADDR_IN addr{};
		int addrsize = sizeof(addr);
		SOCKET id = WSAAccept(listenSocket, (sockaddr*)&addr, &addrsize, NULL, NULL);

		//	//clients socket process
		clients[id] = Client{};
		clients[id].socket = id;
		clients[id].x = clients[id].y = 0;
		clients[id].wsaBuf.buf = clients[id].messageBuffer;
		clients[id].wsaBuf.len = BUF_SIZE;
		ZeroMemory(&clients[id].overlapped, sizeof(clients[id].overlapped));
		clients[id].overlapped.hEvent = (HANDLE)id;
		DWORD flags = 0;
		int recvBytes = WSARecv(clients[id].socket, &clients[id].wsaBuf, 1, NULL, &flags, &clients[id].overlapped, Recv_Complete);
	}
	closesocket(listenSocket);
	WSACleanup();
}