#include "pch.h"
#include "BeigeFire.h"

namespace
{
	int32 AxialDistance(const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs)
	{
		const int32 dq = lhs.q() - rhs.q();
		const int32 dr = lhs.r() - rhs.r();
		const int32 ds = -dq - dr;
		return (abs(dq) + abs(dr) + abs(ds)) / 2;
	}

	int32 FindDirectionIndex(const Protocol::AxialCoord& source, const Protocol::AxialCoord& target,
		const int32 directions[6][2])
	{
		const int32 distance = AxialDistance(source, target);
		for (int32 i = 0; i < 6; i++)
		{
			Protocol::AxialCoord step;
			step.set_q(source.q() + directions[i][0]);
			step.set_r(source.r() + directions[i][1]);
			if (AxialDistance(step, target) == distance - 1)
				return i;
		}

		return 0;
	}
}

vector<Protocol::AxialCoord> BeigeFire::ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target,
	const Protocol::AxialCoord* directionTarget) const
{
	vector<Protocol::AxialCoord> area = BattlePawn::ResolveTargetArea(shape, target, directionTarget);
	static constexpr int32 kDirections[6][2] =
	{
		{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
	};

	if (shape != "LINE_3" || directionTarget == nullptr)
		return area;

	const int32 directionIndex = FindDirectionIndex(target, *directionTarget, kDirections);
	for (int32 distance = 1; distance <= 2; distance++)
	{
		Protocol::AxialCoord next;
		next.set_q(target.q() + kDirections[directionIndex][0] * distance);
		next.set_r(target.r() + kDirections[directionIndex][1] * distance);
		area.push_back(next);
	}

	return area;
}
