#include "Sector.h"

Sector::~Sector()
{
}

bool Sector::isInSector(int idx)
{
	if (clientList.count(idx) > 0)
		return true;
	return false;
}

void Sector::insertClient(int id)
{
	clientList.insert(id);
}

void Sector::removeClient(int id)
{
	clientList.erase(id);
}