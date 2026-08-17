-- Adds the expedition happiness clock, thirst clock, and persistent water
-- stored inside reusable bottle inventory stacks.

SET @happiness_exists = (
    SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_profiles' AND COLUMN_NAME = 'happiness'
);
SET @happiness_sql = IF(
    @happiness_exists = 0,
    'ALTER TABLE player_profiles ADD COLUMN happiness INT NOT NULL DEFAULT 100 AFTER max_satiety, ADD COLUMN max_happiness INT NOT NULL DEFAULT 100 AFTER happiness, ADD COLUMN happiness_drain_numerator BIGINT NOT NULL DEFAULT 0 AFTER satiety_drain_numerator, ADD COLUMN thirst_drain_numerator BIGINT NOT NULL DEFAULT 0 AFTER happiness_drain_numerator',
    'SELECT 1'
);
PREPARE happiness_statement FROM @happiness_sql;
EXECUTE happiness_statement;
DEALLOCATE PREPARE happiness_statement;

SET @water_charge_exists = (
    SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'player_inventory_stacks' AND COLUMN_NAME = 'water_charge'
);
SET @water_charge_sql = IF(
    @water_charge_exists = 0,
    'ALTER TABLE player_inventory_stacks ADD COLUMN water_charge INT NOT NULL DEFAULT 0 AFTER quantity',
    'SELECT 1'
);
PREPARE water_charge_statement FROM @water_charge_sql;
EXECUTE water_charge_statement;
DEALLOCATE PREPARE water_charge_statement;
