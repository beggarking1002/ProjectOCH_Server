-- Initial persistent account, expedition economy, inventory, and player-scoped
-- village stock schema. Statements must remain safe to retry because MySQL DDL
-- can commit implicitly before the migration history row is recorded.

CREATE TABLE IF NOT EXISTS accounts (
    account_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    email VARCHAR(320) NOT NULL DEFAULT '',
    display_name VARCHAR(128) NOT NULL DEFAULT 'Player',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_login_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB AUTO_INCREMENT=1000000000000 DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS account_identities (
    account_id BIGINT UNSIGNED NOT NULL,
    provider VARCHAR(32) NOT NULL,
    provider_subject VARCHAR(255) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (provider, provider_subject),
    KEY idx_account_identities_account (account_id),
    CONSTRAINT fk_account_identity_account FOREIGN KEY (account_id)
        REFERENCES accounts(account_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS player_profiles (
    player_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    gold INT NOT NULL,
    satiety INT NOT NULL,
    max_satiety INT NOT NULL,
    thirst INT NOT NULL,
    max_thirst INT NOT NULL,
    satiety_drain_numerator BIGINT NOT NULL,
    next_inventory_stack_id BIGINT UNSIGNED NOT NULL,
    next_acquired_sequence BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS player_inventory_stacks (
    player_id BIGINT UNSIGNED NOT NULL,
    stack_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(64) NOT NULL,
    quantity INT NOT NULL,
    remaining_shelf_life_ms BIGINT NOT NULL,
    acquired_sequence BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (player_id, stack_id),
    CONSTRAINT fk_player_inventory_profile FOREIGN KEY (player_id)
        REFERENCES player_profiles(player_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Reserved for the player-scoped village stock implementation.
CREATE TABLE IF NOT EXISTS player_village_shop_stock (
    player_id BIGINT UNSIGNED NOT NULL,
    village_id VARCHAR(64) NOT NULL,
    item_id VARCHAR(64) NOT NULL,
    stock INT NOT NULL,
    stock_generation BIGINT UNSIGNED NOT NULL DEFAULT 0,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (player_id, village_id, item_id),
    CONSTRAINT fk_player_shop_profile FOREIGN KEY (player_id)
        REFERENCES player_profiles(player_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
