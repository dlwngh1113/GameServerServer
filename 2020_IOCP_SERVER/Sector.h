#pragma once
#include <iostream>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include"protocol.h"

constexpr int MAX_BUFFER = 4096;

using namespace std;

struct OVER_EX {
	WSAOVERLAPPED wsa_over;
	char	op_mode;
	WSABUF	wsa_buf;
	unsigned char iocp_buf[MAX_BUFFER];
};

struct client_info {
	mutex c_lock;
	char name[MAX_ID_LEN];
	short x, y;

	bool in_use;
	SOCKET	m_sock;
	OVER_EX	m_recv_over;
	unsigned char* m_packet_start;
	unsigned char* m_recv_start;

	mutex vl;
	unordered_set<int> view_list;

	int move_time;
};

class Sector
{
	RECT rect;
	unordered_set<int> clientList;

	mutex s_lock;
public:
	Sector(int x, int y, int w, int h);
	virtual ~Sector();
	bool isInSector(int idx);
	bool isInSector(int x, int y);
	void insertClient(int id);
	void removeClient(int id);
};

