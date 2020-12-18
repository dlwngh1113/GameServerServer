#include <iostream>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <queue>
#include<string>
#include <windows.h>  
#include <stdio.h>  
#include <sqlext.h>

extern "C" {
#include "include/lua.h"
#include "include/lauxlib.h"
#include "include/lualib.h"
}

#include "protocol.h"
using namespace std;
using namespace chrono;
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")
#pragma commint(lib, "odbc32")

constexpr int MAX_BUFFER = 4096;

constexpr char OP_MODE_RECV = 0;
constexpr char OP_MODE_SEND = 1;
constexpr char OP_MODE_ACCEPT = 2;
constexpr char OP_RANDOM_MOVE = 3;
constexpr char OP_PLAYER_MOVE_NOTIFY = 4;
constexpr char OP_RUNAWAY = 5;

constexpr int  KEY_SERVER = 1000000;

struct OVER_EX {
	WSAOVERLAPPED wsa_over;
	char	op_mode;
	WSABUF	wsa_buf;
	unsigned char iocp_buf[MAX_BUFFER];
	int		object_id;
};

struct client_info {
	mutex c_lock;
	char name[MAX_ID_LEN];
	short level;
	short hp;
	short x, y;
	int exp;
	lua_State* L;

	bool in_use;
	atomic_bool is_active;
	SOCKET	m_sock;
	OVER_EX	m_recv_over;
	unsigned char* m_packet_start;
	unsigned char* m_recv_start;

	mutex vl;
	unordered_set <int> view_list;

	int move_time;
};

mutex id_lock;
client_info g_clients[MAX_USER + NUM_NPC];
HANDLE		h_iocp;

SOCKET g_lSocket;
OVER_EX g_accept_over;

SQLHENV henv;
SQLHDBC hdbc;
SQLHSTMT hstmt = 0;
SQLRETURN dbRetcode;

struct event_type {
	int obj_id;
	system_clock::time_point wakeup_time;
	int event_id;
	int target_id;
	char* message;

	constexpr bool operator < (const event_type& _Left) const
	{
		return (wakeup_time > _Left.wakeup_time);
	}
};

priority_queue<event_type> timer_queue;
mutex timer_l;

void add_timer(int obj_id, int ev_type, system_clock::time_point t, int target_id = NULL, char* mess = NULL)
{
	event_type ev{ obj_id, t, ev_type, target_id, mess };
	timer_l.lock();
	timer_queue.push(ev);
	timer_l.unlock();
}

void random_move_npc(int id);
void send_chat_packet(int to_client, int id, char* mess);
void disconnect_client(int id);

void time_worker()
{
	while (true) {
		while (true) {
			if (false == timer_queue.empty()) {
				event_type ev = timer_queue.top();
				if (ev.wakeup_time > system_clock::now()) break;
				timer_l.lock();
				timer_queue.pop();
				timer_l.unlock();

				switch (ev.event_id)
				{
				case OP_RANDOM_MOVE:
				{
					//random_move_npc(ev.obj_id);
					OVER_EX* over_ex = new OVER_EX;
					over_ex->op_mode = OP_RANDOM_MOVE;
					PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &over_ex->wsa_over);
					//add_timer(ev.obj_id, OP_RANDOM_MOVE, system_clock::now() + 1s);
				}
					break;
				case OP_RUNAWAY:
				{
					send_chat_packet(ev.target_id, ev.obj_id, ev.message);
				}
					break;
				default:
					printf("Unknown event type: %c\n", ev.event_id);
					break;
				}
			}
			else break;
		}
		this_thread::sleep_for(1ms);
	}
}

void wake_up_npc(int id)
{
	bool b = false;
	if (true == g_clients[id].is_active.compare_exchange_strong(b, true))
	{
		add_timer(id, OP_RANDOM_MOVE, system_clock::now() + 1s);
	}
}

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::cout << msg;
	std::wcout << L"에러 " << lpMsgBuf << std::endl;
	while (true);
	LocalFree(lpMsgBuf);
}

bool is_npc(int p1)
{
	return p1 >= MAX_USER;
}
bool is_near(int p1, int p2)
{
	int dist = (g_clients[p1].x - g_clients[p2].x) * (g_clients[p1].x - g_clients[p2].x);
	dist += (g_clients[p1].y - g_clients[p2].y) * (g_clients[p1].y - g_clients[p2].y);

	return dist <= VIEW_LIMIT * VIEW_LIMIT;
}
void send_packet(int id, void* p)
{
	unsigned char* packet = reinterpret_cast<unsigned char *>(p);
	OVER_EX* send_over = new OVER_EX;
	memcpy(send_over->iocp_buf, packet, packet[0]);
	send_over->op_mode = OP_MODE_SEND;
	send_over->wsa_buf.buf = reinterpret_cast<CHAR *>(send_over->iocp_buf);
	send_over->wsa_buf.len = packet[0];
	ZeroMemory(&send_over->wsa_over, sizeof(send_over->wsa_over));
	g_clients[id].c_lock.lock();
	if (true == g_clients[id].in_use)
		WSASend(g_clients[id].m_sock, &send_over->wsa_buf, 1,
			NULL, 0, &send_over->wsa_over, NULL);
	g_clients[id].c_lock.unlock();
}

void send_chat_packet(int to_client, int id, char *mess)
{
	sc_packet_chat p;
	p.id = id;
	p.size = sizeof(p);
	p.type = SC_PACKET_CHAT;
	strcpy_s(p.message, mess);
	send_packet(to_client, &p);
}

void send_login_ok(int id)
{
	sc_packet_login_ok p;
	p.exp = g_clients[id].exp;
	p.hp = g_clients[id].hp;
	p.id = id;
	p.level = g_clients[id].level;
	p.size = sizeof(p);
	p.type = SC_PACKET_LOGIN_OK;
	p.x = g_clients[id].x;
	p.y = g_clients[id].y;
	send_packet(id, &p);
}

void send_move_packet(int to_client, int id)
{
	sc_packet_move p;
	p.id = id;
	p.size = sizeof(p);
	p.type = SC_PACKET_MOVE;
	p.x = g_clients[id].x;
	p.y = g_clients[id].y;
	p.move_time = g_clients[id].move_time;
	send_packet(to_client, &p);
}

void send_enter_packet(int to_client, int new_id)
{
	sc_packet_enter p;
	p.id = new_id;
	p.size = sizeof(p);
	p.type = SC_PACKET_ENTER;
	p.x = g_clients[new_id].x;
	p.y = g_clients[new_id].y;
	g_clients[new_id].c_lock.lock();
	strcpy_s(p.name, g_clients[new_id].name);
	g_clients[new_id].c_lock.unlock();
	p.o_type = 0;
	send_packet(to_client, &p);
}

void send_leave_packet(int to_client, int new_id)
{
	sc_packet_leave p;
	p.id = new_id;
	p.size = sizeof(p);
	p.type = SC_PACKET_LEAVE;
	send_packet(to_client, &p);
}

void process_move(int id, char dir)
{
	short y = g_clients[id].y;
	short x = g_clients[id].x;
	switch (dir) {
	case MV_UP: if (y > 0) y--; break;
	case MV_DOWN: if (y < (WORLD_HEIGHT - 1)) y++; break;
	case MV_LEFT: if (x > 0) x--; break;
	case MV_RIGHT: if (x < (WORLD_WIDTH - 1)) x++; break;
	default: cout << "Unknown Direction in CS_MOVE packet.\n";
		while (true);
	}
	unordered_set <int> old_viewlist = g_clients[id].view_list;
		
	g_clients[id].x = x;
	g_clients[id].y = y;

	send_move_packet(id, id);

	unordered_set <int> new_viewlist;
	for (int i = 0; i < MAX_USER; ++i) {
		if (id == i) continue;
		if (false == g_clients[i].in_use) continue;
		if (true == is_near(id, i)) new_viewlist.insert(i);
	}

	for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i) {
		if (true == is_near(id, i)) new_viewlist.insert(i);
		wake_up_npc(i);
	}

	// 시야에 들어온 객체 처리
	for (int ob : new_viewlist) {
		if (0 == old_viewlist.count(ob)) {
			g_clients[id].vl.lock();
			g_clients[id].view_list.insert(ob);
			send_enter_packet(id, ob);
			g_clients[id].vl.unlock();

			if (false == is_npc(ob)) {
				if (0 == g_clients[ob].view_list.count(id)) {
					g_clients[ob].vl.lock();
					g_clients[ob].view_list.insert(id);
					send_enter_packet(ob, id);
					g_clients[ob].vl.unlock();
				}
				else {
					send_move_packet(ob, id);
				}
			}
		}
		else {  // 이전에도 시야에 있었고, 이동후에도 시야에 있는 객체
			if (false == is_npc(ob)) {
				if (0 != g_clients[ob].view_list.count(id)) {
					send_move_packet(ob, id);
				}
				else
				{
					g_clients[ob].vl.lock();
					g_clients[ob].view_list.insert(id);
					send_enter_packet(ob, id);
					g_clients[ob].vl.unlock();
				}
			}
		}
	}
	for (int ob : old_viewlist) {
		if (0 == new_viewlist.count(ob)) {
			g_clients[id].vl.lock();
			g_clients[id].view_list.erase(ob);
			send_leave_packet(id, ob);
			g_clients[id].vl.unlock();
			if (false == is_npc(ob)) {
				if (0 != g_clients[ob].view_list.count(id)) {
					g_clients[ob].vl.lock();
					g_clients[ob].view_list.erase(id);
					send_leave_packet(ob, id);
					g_clients[ob].vl.unlock();
				}
			}
		}
	}

	if (false == is_npc(id)) {
		for (auto& npc : new_viewlist) {
			if (false == is_npc(npc)) continue;
			OVER_EX* ex_over = new OVER_EX;
			ex_over->object_id = id;
			ex_over->op_mode = OP_PLAYER_MOVE_NOTIFY;
			PostQueuedCompletionStatus(h_iocp, 1, npc, &ex_over->wsa_over);
		}
	}
}

void get_userdata(cs_packet_login* p, int id)
{
	dbRetcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	char getQuery[MAX_STR_LEN] = { NULL };
	sprintf_s(getQuery, "EXEC get_userdata %s", p->name);

	int nChars = MultiByteToWideChar(CP_ACP, 0, getQuery, -1, NULL, 0);
	wchar_t* pwcsName = new wchar_t[nChars];
	MultiByteToWideChar(CP_ACP, 0, getQuery, -1, (LPWSTR)pwcsName, nChars);

	dbRetcode = SQLExecDirect(hstmt, (SQLWCHAR*)pwcsName, SQL_NTS);

	if (dbRetcode == SQL_SUCCESS || dbRetcode == SQL_SUCCESS_WITH_INFO) {
		SQLINTEGER LEVEL, EXP;
		SQLSMALLINT POSX, POSY, HP;
		SQLLEN cbLevel, cbExp, cbPosX, cbPosY, cbHp;
		// Bind columns 1, 2, and 3  
		dbRetcode = SQLBindCol(hstmt, 1, SQL_C_LONG, &LEVEL, 100, &cbLevel);
		dbRetcode = SQLBindCol(hstmt, 2, SQL_C_SHORT, &POSX, 100, &cbPosX);
		dbRetcode = SQLBindCol(hstmt, 3, SQL_C_SHORT, &POSY, 100, &cbPosY);
		dbRetcode = SQLBindCol(hstmt, 4, SQL_C_LONG, &EXP, 100, &cbExp);
		dbRetcode = SQLBindCol(hstmt, 5, SQL_C_LONG, &HP, 100, &cbHp);

		dbRetcode = SQLFetch(hstmt);
		if (dbRetcode == SQL_ERROR || dbRetcode == SQL_SUCCESS_WITH_INFO)
			cout << "code line 743 error\n";
		if (dbRetcode == SQL_SUCCESS || dbRetcode == SQL_SUCCESS_WITH_INFO)
		{
			g_clients[id].c_lock.lock();
			strcpy_s(g_clients[id].name, p->name);
			g_clients[id].c_lock.unlock();

			g_clients[id].level = LEVEL;
			g_clients[id].x = POSX;
			g_clients[id].y = POSY;
			g_clients[id].exp = EXP;
			g_clients[id].hp = HP;
			//printf("user[%s] level - %d, x - %hd, y - %hd, exp - %d\n",
			//	g_clients[id].name, g_clients[id].level, g_clients[id].x, g_clients[id].y, g_clients[id].exp);
		}
	}

	delete[] pwcsName;
}

void set_userdata(int id, bool isInit)
{
	dbRetcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	wchar_t tmp[MAX_STR_LEN] = { NULL };

	int nChars = MultiByteToWideChar(CP_ACP, 0, g_clients[id].name, -1, NULL, 0);
	wchar_t* nameWchar = new wchar_t[nChars];
	MultiByteToWideChar(CP_ACP, 0, g_clients[id].name, -1, (LPWSTR)nameWchar, nChars);

	if (isInit) {
		g_clients[id].level = 1;
		g_clients[id].x = 0;
		g_clients[id].y = 0;
		g_clients[id].exp = 0;
		g_clients[id].hp = 100;
		wsprintf(tmp, L"EXEC insert_player %s",	nameWchar);

		dbRetcode = SQLExecDirect(hstmt, (SQLWCHAR*)tmp, SQL_NTS);

		return;
	}

	//이름, 레벨, x, y, exp
	wsprintf(tmp, L"EXEC set_userdata %s, %d, %hd, %hd, %d, %hd",
		nameWchar,
		g_clients[id].level,
		g_clients[id].x,
		g_clients[id].y,
		g_clients[id].exp,
		g_clients[id].hp);
	for (int i = 0; i < lstrlenW(tmp); ++i)
		wcout << tmp[i];
	cout << endl;

	dbRetcode = SQLExecDirect(hstmt, (SQLWCHAR*)tmp, SQL_NTS);

	delete[] nameWchar;
}

void process_packet(int id)
{
	char p_type = g_clients[id].m_packet_start[1];
	switch (p_type) {
	case CS_LOGIN:
	{
		cs_packet_login* p = reinterpret_cast<cs_packet_login *>(g_clients[id].m_packet_start);

		strcpy_s(g_clients[id].name, p->name);
		set_userdata(id, true);

		if(dbRetcode != SQL_SUCCESS && dbRetcode != SQL_SUCCESS_WITH_INFO)
			get_userdata(p, id);

		send_login_ok(id);
		for (int i = 0; i < MAX_USER; ++i)
			if (true == g_clients[i].in_use)
				if (id != i) {
					if (false == is_near(i, id)) continue;
					if (0 == g_clients[i].view_list.count(id)) {
						g_clients[id].vl.lock();
						g_clients[i].view_list.insert(id);
						send_enter_packet(i, id);
						g_clients[id].vl.unlock();
					}
					if (0 == g_clients[id].view_list.count(i)) {
						g_clients[id].vl.lock();
						g_clients[id].view_list.insert(i);
						send_enter_packet(id, i);
						g_clients[id].vl.unlock();
					}
				}
		for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i) {
			if (false == is_near(id, i)) continue;
			g_clients[id].vl.lock();
			g_clients[id].view_list.insert(i);
			send_enter_packet(id, i);
			wake_up_npc(i);
			g_clients[id].vl.unlock();
		}
		break;
	}
	case CS_MOVE: {
		cs_packet_move* p = reinterpret_cast<cs_packet_move*>(g_clients[id].m_packet_start);
		g_clients[id].move_time = p->move_time;
		process_move(id, p->direction);
	}
		break;
	case CS_LOGOUT:
	{
		disconnect_client(id);
	}
	break;
	default: cout << "Unknown Packet type [" << p_type << "] from Client [" << id << "]\n";
		while (true);
	}
}

constexpr int MIN_BUFF_SIZE = 1024;

void process_recv(int id, DWORD iosize)
{
	unsigned char p_size = g_clients[id].m_packet_start[0];
	unsigned char* next_recv_ptr = g_clients[id].m_recv_start + iosize;
	while (p_size <= next_recv_ptr - g_clients[id].m_packet_start) {
		process_packet(id);
		g_clients[id].m_packet_start += p_size;
		if (g_clients[id].m_packet_start < next_recv_ptr)
			p_size = g_clients[id].m_packet_start[0];
		else break;
	}

	long long left_data = next_recv_ptr - g_clients[id].m_packet_start;

	if ((MAX_BUFFER - (next_recv_ptr - g_clients[id].m_recv_over.iocp_buf))
		< MIN_BUFF_SIZE) {
		memcpy(g_clients[id].m_recv_over.iocp_buf,
			g_clients[id].m_packet_start, left_data);
		g_clients[id].m_packet_start = g_clients[id].m_recv_over.iocp_buf;
		next_recv_ptr = g_clients[id].m_packet_start + left_data;
	}
	DWORD recv_flag = 0;
	g_clients[id].m_recv_start = next_recv_ptr;
	g_clients[id].m_recv_over.wsa_buf.buf = reinterpret_cast<CHAR *>(next_recv_ptr);
	g_clients[id].m_recv_over.wsa_buf.len = MAX_BUFFER - 
		static_cast<int>(next_recv_ptr - g_clients[id].m_recv_over.iocp_buf);

	g_clients[id].c_lock.lock();
	if (true == g_clients[id].in_use) {
		WSARecv(g_clients[id].m_sock, &g_clients[id].m_recv_over.wsa_buf,
			1, NULL, &recv_flag, &g_clients[id].m_recv_over.wsa_over, NULL);
	}
	g_clients[id].c_lock.unlock();
}

void add_new_client(SOCKET ns)
{
	int i;
	id_lock.lock();
	for (i = 0; i < MAX_USER; ++i)
		if (false == g_clients[i].in_use) break;
	id_lock.unlock();
	if (MAX_USER == i) {
		cout << "Max user limit exceeded.\n";
		closesocket(ns);
	}
	else {
		// cout << "New Client [" << i << "] Accepted" << endl;
		g_clients[i].c_lock.lock();
		g_clients[i].in_use = true;
		g_clients[i].m_sock = ns;
		g_clients[i].name[0] = 0;
		g_clients[i].c_lock.unlock();

		g_clients[i].m_packet_start = g_clients[i].m_recv_over.iocp_buf;
		g_clients[i].m_recv_over.op_mode = OP_MODE_RECV;
		g_clients[i].m_recv_over.wsa_buf.buf 
			= reinterpret_cast<CHAR *>(g_clients[i].m_recv_over.iocp_buf);
		g_clients[i].m_recv_over.wsa_buf.len = sizeof(g_clients[i].m_recv_over.iocp_buf);
		ZeroMemory(&g_clients[i].m_recv_over.wsa_over, sizeof(g_clients[i].m_recv_over.wsa_over));
		g_clients[i].m_recv_start = g_clients[i].m_recv_over.iocp_buf;

		g_clients[i].x = rand() % WORLD_WIDTH;
		g_clients[i].y = rand() % WORLD_HEIGHT;
		CreateIoCompletionPort(reinterpret_cast<HANDLE>(ns), h_iocp, i, 0);
		DWORD flags = 0;
		int ret;
		g_clients[i].c_lock.lock();
		if (true == g_clients[i].in_use) {
			ret = WSARecv(g_clients[i].m_sock, &g_clients[i].m_recv_over.wsa_buf, 1, NULL,
				&flags, &g_clients[i].m_recv_over.wsa_over, NULL);
		}
		g_clients[i].c_lock.unlock();
		if (SOCKET_ERROR == ret) {
			int err_no = WSAGetLastError();
			if (ERROR_IO_PENDING != err_no)
				error_display("WSARecv : ", err_no);
		}
	}
	SOCKET cSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_accept_over.op_mode = OP_MODE_ACCEPT;
	g_accept_over.wsa_buf.len = static_cast<ULONG> (cSocket);
	ZeroMemory(&g_accept_over.wsa_over, sizeof(&g_accept_over.wsa_over));
	AcceptEx(g_lSocket, cSocket, g_accept_over.iocp_buf, 0, 32, 32, NULL, &g_accept_over.wsa_over);
}

void disconnect_client(int id)
{
	for (int i = 0; i < MAX_USER; ++i) {
		if (true == g_clients[i].in_use)
			if (i != id) {
				if (0 != g_clients[i].view_list.count(id)) {
					g_clients[i].view_list.erase(id);
					send_leave_packet(i, id);
				}
			}
	}
	g_clients[id].c_lock.lock();
	set_userdata(id, false);
	g_clients[id].in_use = false;
	g_clients[id].view_list.clear();
	closesocket(g_clients[id].m_sock);
	g_clients[id].m_sock = 0;
	g_clients[id].c_lock.unlock();
}

void worker_thread()
{
	// 반복
	//   - 이 쓰레드를 IOCP thread pool에 등록  => GQCS
	//   - iocp가 처리를 맞긴 I/O완료 데이터를 꺼내기 => GQCS
	//   - 꺼낸 I/O완료 데이터를 처리
	while (true) {
		DWORD io_size;
		int key;
		ULONG_PTR iocp_key;
		WSAOVERLAPPED* lpover;
		int ret = GetQueuedCompletionStatus(h_iocp, &io_size, &iocp_key, &lpover, INFINITE);
		key = static_cast<int>(iocp_key);
		// cout << "Completion Detected" << endl;
		if (FALSE == ret) {
			error_display("GQCS Error : ", WSAGetLastError());
		}

		OVER_EX* over_ex = reinterpret_cast<OVER_EX*>(lpover);
		switch (over_ex->op_mode) {
		case OP_MODE_ACCEPT:
			add_new_client(static_cast<SOCKET>(over_ex->wsa_buf.len));
			break;
		case OP_MODE_RECV:
			if (0 == io_size)
				disconnect_client(key);
			else {
				// cout << "Packet from Client [" << key << "]" << endl;
				process_recv(key, io_size);
			}
			break;
		case OP_MODE_SEND:
			delete over_ex;
			break;
		case OP_RANDOM_MOVE:
			random_move_npc(key);
			break;
		case OP_PLAYER_MOVE_NOTIFY:
			lua_getglobal(g_clients[key].L, "event_player_move");
			lua_pushnumber(g_clients[key].L, over_ex->object_id);
			lua_pcall(g_clients[key].L, 1, 1, 0);
			if(over_ex)
				delete over_ex;
			break;
		}
	}
}

int API_get_x(lua_State* L)
{
	int user_id = lua_tointeger(L, -1);
	lua_pop(L, 2);
	int x = g_clients[user_id].x;
	lua_pushnumber(L, x);
	return 1;
}

int API_get_y(lua_State* L)
{
	int user_id = lua_tointeger(L, -1);
	lua_pop(L, 2);
	int y = g_clients[user_id].y;
	lua_pushnumber(L, y);
	return 1;
}

int API_SendEnterMessage(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);
	int user_id = (int)lua_tointeger(L, -2);
	char* mess = (char*)lua_tostring(L, -1);

	lua_pop(L, 3);

	send_chat_packet(user_id, my_id, mess);
	return 0;
}

int API_SendLeaveMessage(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);
	int user_id = (int)lua_tointeger(L, -2);
	char* mess = (char*)lua_tostring(L, -1);

	lua_pop(L, 3);

	add_timer(my_id, OP_RUNAWAY, system_clock::now() + 3s, user_id, mess);

	return 0;
}

void initialize_NPC()
{
	cout << "Initializing NPCs\n";
	for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i)
	{
		g_clients[i].x = rand() % WORLD_WIDTH;
		g_clients[i].y = rand() % WORLD_HEIGHT;
		char npc_name[50];
		sprintf_s(npc_name, "N%d", i);
		strcpy_s(g_clients[i].name, npc_name);
		g_clients[i].is_active = false;

		lua_State* L = g_clients[i].L = luaL_newstate();
		luaL_openlibs(L);

		int error = luaL_loadfile(L, "monster.lua");
		error = lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, i);
		lua_pcall(L, 1, 1, 0);
		// lua_pop(L, 1);// eliminate set_uid from stack after call

		lua_register(L, "API_SendEnterMessage", API_SendEnterMessage);
		lua_register(L, "API_SendLeaveMessage", API_SendLeaveMessage);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
	}
	cout << "NPC initialize finished.\n";
}

void random_move_npc(int id)
{
	unordered_set <int> old_viewlist;
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == g_clients[i].in_use) continue;
		if (true == is_near(id, i)) old_viewlist.insert(i);
	}
	int x = g_clients[id].x;
	int y = g_clients[id].y;
	switch (rand() % 4)
	{
	case 0: if (x > 0) x--; break;
	case 1: if (x < (WORLD_WIDTH - 1)) x++; break;
	case 2: if (y > 0) y--; break;
	case 3: if (y < (WORLD_HEIGHT - 1)) y++; break;
	}
	g_clients[id].x = x;
	g_clients[id].y = y;
	unordered_set <int> new_viewlist;
	for (int i = 0; i < MAX_USER; ++i) {
		if (id == i) continue;
		if (false == g_clients[i].in_use) continue;
		if (true == is_near(id, i)) new_viewlist.insert(i);
	}

	for (auto pl : old_viewlist) {
		if (0 < new_viewlist.count(pl)) {
			if (0 < g_clients[pl].view_list.count(id))
				send_move_packet(pl, id);
			else {
				g_clients[pl].view_list.insert(id);
					send_enter_packet(pl, id);
			}
		}
		else
		{
			if (0 < g_clients[pl].view_list.count(id)) {
				g_clients[pl].view_list.erase(id);
				send_leave_packet(pl, id);
			}
		}
	}

	for (auto pl : new_viewlist) {
		if (0 == g_clients[pl].view_list.count(pl)) {
			if (0 == g_clients[pl].view_list.count(id)) {
				g_clients[pl].view_list.insert(id);
				send_enter_packet(pl, id);
			}
			else
				send_move_packet(pl, id);
		}
	}

	if (true == new_viewlist.empty()) {
		g_clients[id].is_active = false;
	}
	else {
		add_timer(id, OP_RANDOM_MOVE, system_clock::now() + 1s);
	}

	for (auto pc : new_viewlist) {
		OVER_EX* over_ex = new OVER_EX;
		over_ex->object_id = pc;
		over_ex->op_mode = OP_PLAYER_MOVE_NOTIFY;
		PostQueuedCompletionStatus(h_iocp, 1, id, &over_ex->wsa_over);
	}
}

void npc_ai_thread()
{
	while (true) {
		auto start_time = system_clock::now();
		for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i)
			random_move_npc(i);
		auto end_time = system_clock::now();
		auto exec_time = end_time - start_time;
		cout << "AI exec time = " << duration_cast<seconds>(exec_time).count() << "s\n";
		this_thread::sleep_for(1s - (end_time - start_time));
	}
}

int main()
{
	std::wcout.imbue(std::locale("korean"));
	for (auto& cl : g_clients)
		cl.in_use = false;

	dbRetcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
	dbRetcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);
	dbRetcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
	SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
	dbRetcode = SQLConnect(hdbc, (SQLWCHAR*)L"g_server_1", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 0), &WSAData);
	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	g_lSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_lSocket), h_iocp, KEY_SERVER, 0);

	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	::bind(g_lSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
	listen(g_lSocket, 5);

	SOCKET cSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_accept_over.op_mode = OP_MODE_ACCEPT;
	g_accept_over.wsa_buf.len = static_cast<int>(cSocket);
	ZeroMemory(&g_accept_over.wsa_over, sizeof(&g_accept_over.wsa_over));
	AcceptEx(g_lSocket, cSocket, g_accept_over.iocp_buf, 0, 32, 32, NULL, &g_accept_over.wsa_over);

	initialize_NPC();

	//thread ai_thread{ npc_ai_thread };
	thread timer_thread{ time_worker };
	vector <thread> worker_threads;
	for (int i = 0; i < 6; ++i) 
		worker_threads.emplace_back(worker_thread);
	for (auto& th : worker_threads)
		th.join();
	//ai_thread.join();
	timer_thread.join();

	SQLCancel(hstmt);
	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	SQLDisconnect(hdbc);
	SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
	SQLFreeHandle(SQL_HANDLE_ENV, henv);
	closesocket(g_lSocket);
	WSACleanup();
}
