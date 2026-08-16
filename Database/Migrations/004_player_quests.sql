CREATE TABLE IF NOT EXISTS player_quests (
    player_id BIGINT UNSIGNED NOT NULL,
    quest_id VARCHAR(128) NOT NULL,
    status VARCHAR(16) NOT NULL,
    accepted_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    ready_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    completed_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (player_id, quest_id),
    CONSTRAINT fk_player_quests_profile
        FOREIGN KEY (player_id) REFERENCES player_profiles(player_id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS player_quest_objectives (
    player_id BIGINT UNSIGNED NOT NULL,
    quest_id VARCHAR(128) NOT NULL,
    objective_index INT UNSIGNED NOT NULL,
    progress INT NOT NULL DEFAULT 0,
    PRIMARY KEY (player_id, quest_id, objective_index),
    CONSTRAINT fk_player_quest_objectives_quest
        FOREIGN KEY (player_id, quest_id) REFERENCES player_quests(player_id, quest_id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
