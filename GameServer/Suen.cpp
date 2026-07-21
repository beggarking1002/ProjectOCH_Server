#include "pch.h"
#include "Suen.h"

bool Suen::HasActiveStatus(const string& statusKey) const
{
	auto statusIt = statuses.find(statusKey);
	return statusIt != statuses.end() && statusIt->second.remainingOwnerTurns != 0;
}
