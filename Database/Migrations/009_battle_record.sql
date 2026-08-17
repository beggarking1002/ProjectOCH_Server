-- Persist per-account PvP battle outcomes for the field status panel.

SET @battle_wins_exists = (
    SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_profiles' AND COLUMN_NAME = 'battle_wins'
);
SET @battle_wins_sql = IF(
    @battle_wins_exists = 0,
    'ALTER TABLE player_profiles ADD COLUMN battle_wins INT NOT NULL DEFAULT 0 AFTER fame',
    'SELECT 1'
);
PREPARE battle_wins_statement FROM @battle_wins_sql;
EXECUTE battle_wins_statement;
DEALLOCATE PREPARE battle_wins_statement;

SET @battle_losses_exists = (
    SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_profiles' AND COLUMN_NAME = 'battle_losses'
);
SET @battle_losses_sql = IF(
    @battle_losses_exists = 0,
    'ALTER TABLE player_profiles ADD COLUMN battle_losses INT NOT NULL DEFAULT 0 AFTER battle_wins',
    'SELECT 1'
);
PREPARE battle_losses_statement FROM @battle_losses_sql;
EXECUTE battle_losses_statement;
DEALLOCATE PREPARE battle_losses_statement;
