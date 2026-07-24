#pragma once

#include "BattlePawn.h"

// Owns generic turn queue construction/progression.  BattleRoom remains
// responsible for room-specific turn-start effects and packet delivery.
class BattleTurnService
{
public:
	vector<uint64> BuildQueue(const vector<BattlePawnRef>& alliedPawns, const vector<BattlePawnRef>& enemyPawns,
		bool isPvp) const;
	uint64 Advance(vector<uint64>& turnQueue, size_t& turnQueueIndex, uint64& currentTurnPawnId,
		const function<BattlePawn*(uint64)>& findPawn, const function<vector<uint64>()>& rebuildQueue) const;

private:
	bool IsAlive(const BattlePawn* pawn) const;
};
