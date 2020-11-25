#include "Sector.h"

Sector::Sector(int x, int y, int w, int h) :rect{ x,y,x + w,y + h }
{

}

Sector::~Sector()
{
}

bool Sector::isInSector(int idx)
{
	if (clientList.count(idx) > 0)
		return true;
	return false;
}

bool Sector::isInSector(int x, int y)
{
	if (x < rect.left)
		return false;
	else if (x > rect.right)
		return false;
	if (y > rect.bottom)
		return false;
	else if (y < rect.top)
		return false;

	return true;
}

void Sector::insertClient(int id)
{
	//s_lock.lock();
	clientList.insert(id);
	//s_lock.unlock();
}

void Sector::removeClient(int id)
{
	//s_lock.lock();
	clientList.erase(id);
	//s_lock.unlock();
}