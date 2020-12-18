#pragma once
#include<WinSock2.h>
#include"protocol.h"

void random_move_npc(int id);
void wake_up_npc(int id);
void error_display(const char* msg, int err_no);
bool is_npc(int p1);
bool is_near(int p1, int p2);
void send_packet(int id, void* p);
void send_chat_packet(int to_client, int id, char* mess);
void send_login_ok(int id);
void send_login_fail(int id);
void send_move_packet(int to_client, int id);
void send_enter_packet(int to_client, int new_id);
void send_leave_packet(int to_client, int new_id);
void send_stat_change(int to_client);
void process_move(int id, char dir); 
void process_status(int player, int target);
void get_userdata(cs_packet_login* p, int id);
void set_userdata(int id, bool isInit);
bool isIn_atkRange(int p1, int p2);
void process_attack(int id);
void process_packet(int id);
void process_recv(int id, DWORD iosize);
void add_new_client(SOCKET ns);
void disconnect_client(int id);