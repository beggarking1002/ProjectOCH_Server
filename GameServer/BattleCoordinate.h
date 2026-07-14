#pragma once

#include "Struct.pb.h"

struct BattleCellCoord
{
	int32 col = 0;
	int32 row = 0;
};

namespace BattleCoordinate
{
	inline Protocol::AxialCoord CellToAxial(int32 col, int32 row)
	{
		Protocol::AxialCoord axial;
		axial.set_q(col - ((row - (row & 1)) / 2));
		axial.set_r(row);
		return axial;
	}

	inline BattleCellCoord AxialToCell(const Protocol::AxialCoord& axial)
	{
		BattleCellCoord cell;
		cell.col = axial.q() + ((axial.r() - (axial.r() & 1)) / 2);
		cell.row = axial.r();
		return cell;
	}
}
