-- Player-specific shop restock clocks advance only while that player is in the
-- game. Existing profiles begin with a fresh 70-minute cycle; their stock rows
-- are initialized from CSV templates by the game server on the next login.

CREATE TABLE IF NOT EXISTS player_shop_states (
    player_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    active_elapsed_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    stock_generation BIGINT UNSIGNED NOT NULL DEFAULT 1,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_player_shop_state_profile FOREIGN KEY (player_id)
        REFERENCES player_profiles(player_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO player_shop_states (player_id, active_elapsed_ms, stock_generation)
SELECT player_id, 0, 1
FROM player_profiles;
