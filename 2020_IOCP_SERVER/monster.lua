myid = 99999;

function set_uid( x )
	-- body
	myid = x;
end

function event_player_move( player )
	-- body
	player_x = API_get_x(player);
	player_y = API_get_y(player);
	my_x = API_get_x(myid);
	my_y = API_get_y(myid);
	print"LUA:";
	print(player_x, player_y, my_x, my_y);
	if(player_x == my_x)then
		if(player_y == my_y) then
		API_SendEnterMessage(myid, player, "HELLO");
		API_SendLeaveMessage(myid, player, "BYE");
		end
	end
end
