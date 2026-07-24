#include "pch.h"
#include "AlenSpear.h"

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

vector<Protocol::AxialCoord> AlenSpear::ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target,
	const Protocol::AxialCoord* directionTarget) const
{
	vector<Protocol::AxialCoord> area = BattlePawn::ResolveTargetArea(shape, target, directionTarget);
	if (shape != "LINE_2")
		return area;

	static constexpr int32 kDirections[6][2] =
	{
		{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
	};

	const int32 distance = AxialDistance(axial, target);
	if (distance <= 0)
		return area;

	for (const auto& direction : kDirections)
	{
		Protocol::AxialCoord step;
		step.set_q(axial.q() + direction[0]);
		step.set_r(axial.r() + direction[1]);
		if (AxialDistance(step, target) == distance - 1)
		{
			Protocol::AxialCoord next;
			next.set_q(target.q() + direction[0]);
			next.set_r(target.r() + direction[1]);
			area.push_back(next);
			break;
		}
	}

	return area;
}
