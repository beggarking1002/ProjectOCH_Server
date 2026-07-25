#include "pch.h"
#include "ObjectUtils.h"
#include "Player.h"
#include "GameSession.h"

atomic<int64> ObjectUtils::s_idGenerator = 1;

PlayerRef ObjectUtils::CreatePlayer(GameSessionRef session)
{
	// ID 생성기
	const int64 newId = s_idGenerator.fetch_add(1);

	PlayerRef player = make_shared<Player>();
	player->objectInfo->set_object_id(newId);
	if (newId % 2 == 1)
	{
		player->AddBattlePawn(Protocol::PAWN_CLASS_SUEN_PARVIS);
		player->AddBattlePawn(Protocol::PAWN_CLASS_BEIGE_FIRE);
	}
	else
	{
		player->AddBattlePawn(Protocol::PAWN_CLASS_ZILLIAN_LONGBOW);
		player->AddBattlePawn(Protocol::PAWN_CLASS_ALEN_SPEAR);
	}

	player->session = session;
	session->player.store(player);

	return player;
}

