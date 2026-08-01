#include "pch.h"
#include "Alen.h"

bool Alen::HasActiveStatus(const string& statusKey) const
{
	auto it = statuses.find(statusKey);
	return it != statuses.end() && it->second.remainingOwnerTurns != 0;
}
