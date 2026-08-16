-- Generated board quests are player-scoped instances.  The CSV rows are shared
-- templates; the resolved villages, item, quantity and reward live here so a
-- relog or server restart never rerolls an offered quest.

CREATE TABLE IF NOT EXISTS player_quest_instances (
    player_id BIGINT UNSIGNED NOT NULL,
    quest_id VARCHAR(128) NOT NULL,
    template_id VARCHAR(128) NOT NULL,
    status VARCHAR(16) NOT NULL,
    display_name VARCHAR(255) NOT NULL,
    description TEXT NOT NULL,
    category VARCHAR(32) NOT NULL,
    start_village_id VARCHAR(64) NOT NULL,
    completion_village_id VARCHAR(64) NOT NULL,
    prerequisite_template_id VARCHAR(128) NOT NULL DEFAULT '',
    board_slot INT NOT NULL DEFAULT -1,
    accepted_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    ready_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    completed_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (player_id, quest_id),
    KEY idx_player_quest_board (player_id, start_village_id, board_slot),
    CONSTRAINT fk_player_quest_instances_profile
        FOREIGN KEY (player_id) REFERENCES player_profiles(player_id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS player_quest_instance_objectives (
    player_id BIGINT UNSIGNED NOT NULL,
    quest_id VARCHAR(128) NOT NULL,
    objective_index INT UNSIGNED NOT NULL,
    objective_type VARCHAR(32) NOT NULL,
    target_village_id VARCHAR(64) NOT NULL DEFAULT '',
    target_item_id VARCHAR(64) NOT NULL DEFAULT '',
    required_count INT NOT NULL,
    progress INT NOT NULL DEFAULT 0,
    PRIMARY KEY (player_id, quest_id, objective_index),
    CONSTRAINT fk_player_quest_instance_objectives
        FOREIGN KEY (player_id, quest_id)
        REFERENCES player_quest_instances(player_id, quest_id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS player_quest_instance_rewards (
    player_id BIGINT UNSIGNED NOT NULL,
    quest_id VARCHAR(128) NOT NULL,
    reward_index INT UNSIGNED NOT NULL,
    reward_type VARCHAR(16) NOT NULL,
    target_id VARCHAR(64) NOT NULL DEFAULT '',
    amount INT NOT NULL,
    PRIMARY KEY (player_id, quest_id, reward_index),
    CONSTRAINT fk_player_quest_instance_rewards
        FOREIGN KEY (player_id, quest_id)
        REFERENCES player_quest_instances(player_id, quest_id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
