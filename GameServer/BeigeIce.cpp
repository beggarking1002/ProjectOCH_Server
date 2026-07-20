#include "pch.h"
#include "BeigeIce.h"

namespace
{
	int32 AxialDistance(const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs)
	{
		const int32 dq = lhs.q() - rhs.q();
		const int32 dr = lhs.r() - rhs.r();
		const int32 ds = -dq - dr;
		return (abs(dq) + abs(dr) + abs(ds)) / 2;
	}
}

vector<Protocol::AxialCoord> BeigeIce::ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target) const
{
	vector<Protocol::AxialCoord> area = BattlePawn::ResolveTargetArea(shape, target);
	if (shape != "TRIANGLE_3")
		return area;

	static constexpr int32 kDirections[6][2] =
	{
		{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
	};

	const int32 distance = AxialDistance(axial, target);
	if (distance <= 0)
		return area;

	int32 directionIndex = 0;
	for (int32 i = 0; i < 6; i++)
	{
		Protocol::AxialCoord step;
		step.set_q(axial.q() + kDirections[i][0]);
		step.set_r(axial.r() + kDirections[i][1]);
		if (AxialDistance(step, target) == distance - 1)
		{
			directionIndex = i;
			break;
		}
	}

	for (int32 offsetIndex : { directionIndex, (directionIndex + 1) % 6 })
	{
		Protocol::AxialCoord extra;
		extra.set_q(target.q() + kDirections[offsetIndex][0]);
		extra.set_r(target.r() + kDirections[offsetIndex][1]);
		area.push_back(extra);
	}

	return area;
}
