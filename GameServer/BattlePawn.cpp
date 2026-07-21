#include "pch.h"
#include "BattlePawn.h"
#include "BeigeFire.h"
#include "BeigeIce.h"
#include "SuenAxe.h"

vector<Protocol::AxialCoord> BattlePawn::ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target,
	const Protocol::AxialCoord* /*directionTarget*/) const
{
	vector<Protocol::AxialCoord> area{ target };
	if (shape != "RADIUS_1")
		return area;

	static constexpr int32 kDirections[6][2] =
	{
		{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
	};
	for (const auto& direction : kDirections)
	{
		Protocol::AxialCoord neighbor;
		neighbor.set_q(target.q() + direction[0]);
		neighbor.set_r(target.r() + direction[1]);
		area.push_back(neighbor);
	}

	return area;
}

BattlePawnRef CreateBattlePawn(Protocol::PawnClass pawnClass)
{
	if (pawnClass == Protocol::PAWN_CLASS_BEIGE_FIRE)
		return make_shared<BeigeFire>();

	if (pawnClass == Protocol::PAWN_CLASS_BEIGE_ICE)
		return make_shared<BeigeIce>();

	if (pawnClass == Protocol::PAWN_CLASS_SUEN_AXE_SWORD)
		return make_shared<SuenAxe>();

	return make_shared<BattlePawn>();
}
