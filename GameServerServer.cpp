#define _CRT_SECURE_NO_WARNINGS
#include<iostream>
#include<WS2tcpip.h>
using namespace std;
#pragma comment (lib, "ws2_32.lib")

constexpr int BUF_SIZE = 1024;
constexpr short PORT = 3500;

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

int main()
{
	wcout.imbue(std::locale("korean"));
	WSADATA WSAdata;
	WSAStartup(MAKEWORD(2, 0), &WSAdata);
	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
	SOCKADDR_IN server_a;
	memset(&server_a, 0, sizeof(server_a));
	server_a.sin_family = AF_INET;
	server_a.sin_port = htons(PORT);
	server_a.sin_addr.s_addr = INADDR_ANY;
	::bind(s_socket, (sockaddr*)&server_a, sizeof(server_a));
	listen(s_socket, SOMAXCONN);

	POINT playerPt{ NULL,NULL };

	while (true) {
		SOCKADDR_IN client_addr;
		int a_size = sizeof(client_addr);
		SOCKET c_socket = WSAAccept(s_socket, (sockaddr*)&client_addr, &a_size, NULL, NULL);
		if (SOCKET_ERROR == c_socket)
			error_display("WSAAccept", WSAGetLastError());
		cout << "New Client Accepted\n";
		while (true) {
			char buff[BUF_SIZE + 1];
			WSABUF wsabuf;
			wsabuf.buf = buff;
			wsabuf.len = BUF_SIZE;
			DWORD num_recv;
			DWORD flag = 0;
			WSARecv(c_socket, &wsabuf, 1, &num_recv, &flag, NULL, NULL);
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
			int ret = WSASend(c_socket, &wsabuf, 1, &num_sent, 0, NULL, NULL);
			if (SOCKET_ERROR == ret) {
				error_display("WSASend", WSAGetLastError());
			}
			cout << "Sent " << wsabuf.len << "Bytes [" << buff << "]\n";
		}
		cout << "Client Connection Closed\n";
		closesocket(c_socket);
	}
	closesocket(s_socket);
	WSACleanup();
}