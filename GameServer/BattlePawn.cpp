#include "pch.h"
#include "BattlePawn.h"
#include "BeigeIce.h"

vector<Protocol::AxialCoord> BattlePawn::ResolveTargetArea(const string& /*shape*/, const Protocol::AxialCoord& target) const
{
	return { target };
}

BattlePawnRef CreateBattlePawn(Protocol::PawnClass pawnClass)
{
	if (pawnClass == Protocol::PAWN_CLASS_BEIGE_ICE)
		return make_shared<BeigeIce>();

	return make_shared<BattlePawn>();
}
