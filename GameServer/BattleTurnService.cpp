#include "pch.h"
#include "BattleTurnService.h"

vector<uint64> BattleTurnService::BuildQueue(const vector<BattlePawnRef>& alliedPawns, const vector<BattlePawnRef>& enemyPawns,
	bool isPvp) const
{
	vector<uint64> queue;
	auto appendAlive = [this, &queue](const vector<BattlePawnRef>& pawns)
		{
			for (const BattlePawnRef& pawn : pawns)
			{
				if (IsAlive(pawn.get()))
					queue.push_back(pawn->pawnId);
			}
		};

	appendAlive(alliedPawns);
	if (isPvp)
		appendAlive(enemyPawns);

	static random_device rd;
	static mt19937 rng(rd());
	shuffle(queue.begin(), queue.end(), rng);
	return queue;
}

uint64 BattleTurnService::Advance(vector<uint64>& turnQueue, size_t& turnQueueIndex, uint64& currentTurnPawnId,
	const function<BattlePawn*(uint64)>& findPawn, const function<vector<uint64>()>& rebuildQueue) const
{
	auto rebuild = [&]()
		{
			turnQueue = rebuildQueue();
			turnQueueIndex = 0;
			currentTurnPawnId = turnQueue.empty() ? 0 : turnQueue.front();
		};

	if (turnQueue.empty())
		rebuild();
	if (turnQueue.empty())
		return 0;

	auto currentIt = find(turnQueue.begin(), turnQueue.end(), currentTurnPawnId);
	if (currentIt == turnQueue.end())
	{
		rebuild();
	}
	else
	{
		turnQueueIndex = static_cast<size_t>(distance(turnQueue.begin(), currentIt)) + 1;
		if (turnQueueIndex >= turnQueue.size())
			rebuild();
		else
			currentTurnPawnId = turnQueue[turnQueueIndex];
	}

	BattlePawn* queuedPawn = findPawn(currentTurnPawnId);
	while (queuedPawn != nullptr && IsAlive(queuedPawn) == false)
	{
		turnQueueIndex++;
		if (turnQueueIndex >= turnQueue.size())
			rebuild();
		else
			currentTurnPawnId = turnQueue[turnQueueIndex];
		queuedPawn = findPawn(currentTurnPawnId);
	}

	return currentTurnPawnId;
}

bool BattleTurnService::IsAlive(const BattlePawn* pawn) const
{
	return pawn != nullptr && pawn->isDead == false && pawn->hp > 0;
}
