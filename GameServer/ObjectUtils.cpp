#include "pch.h"
#include "ObjectUtils.h"
#include "Player.h"
#include "GameSession.h"
#include "DatabaseManager.h"

atomic<int64> ObjectUtils::s_idGenerator = 1000000000;

PlayerRef ObjectUtils::CreatePlayer(GameSessionRef session, uint64 persistentPlayerId)
{
	// Google login passes the authenticated account id here. When Google auth is
	// disabled, playerIndex remains available as a development-only identity.
	const uint64 newId = persistentPlayerId != 0
		? persistentPlayerId
		: static_cast<uint64>(s_idGenerator.fetch_add(1));

	PlayerRef player = make_shared<Player>();
	player->objectInfo->set_object_id(newId);
	player->hasPersistentIdentity = persistentPlayerId != 0;
	player->InitializeEconomy(::GetTickCount64());
	if (persistentPlayerId != 0 && GDatabase.IsEnabled())
	{
		PersistentPlayerEconomyState savedState;
		bool found = false;
		if (!GDatabase.LoadPlayerEconomy(persistentPlayerId, savedState, found))
		{
			cout << "[Database] Failed to load player_id=" << persistentPlayerId << ". New state will be used." << endl;
		}
		else if (found)
		{
			player->RestoreEconomyState(savedState, ::GetTickCount64());
		}
		else if (GDatabase.SavePlayerEconomy(persistentPlayerId, player->ExportEconomyState()))
		{
			player->MarkEconomyPersisted(::GetTickCount64());
		}
	}
	// PvP battle pawns are assigned only after both players finish class selection.

	player->session = session;
	session->player.store(player);

	return player;
}

