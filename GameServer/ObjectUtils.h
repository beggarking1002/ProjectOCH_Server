#pragma once

class ObjectUtils
{
public:
	static PlayerRef CreatePlayer(GameSessionRef session, uint64 persistentPlayerId = 0);

private:
	static atomic<int64> s_idGenerator;
};

