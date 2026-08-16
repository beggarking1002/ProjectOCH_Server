-- Fame is a single account/player-profile value.  It replaces the unused
-- per-village reputation placeholder and may become negative through quest
-- abandonment penalties.

SET @player_profile_fame_exists = (
    SELECT COUNT(*)
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'player_profiles'
      AND COLUMN_NAME = 'fame'
);

SET @player_profile_fame_sql = IF(
    @player_profile_fame_exists = 0,
    'ALTER TABLE player_profiles ADD COLUMN fame INT NOT NULL DEFAULT 0 AFTER gold',
    'SELECT 1'
);

PREPARE player_profile_fame_statement FROM @player_profile_fame_sql;
EXECUTE player_profile_fame_statement;
DEALLOCATE PREPARE player_profile_fame_statement;

-- Existing generated quest instances predate fame rewards.  Keep their
-- economic contract aligned with newly generated instances.
INSERT IGNORE INTO player_quest_instance_rewards
    (player_id, quest_id, reward_index, reward_type, target_id, amount)
SELECT player_id, quest_id, 2, 'FAME', '',
    CASE template_id
        WHEN 'quest_pool_route_scout' THEN 5
        WHEN 'quest_pool_food_delivery' THEN 8
        WHEN 'quest_pool_trade_delivery' THEN 12
        ELSE 5
    END
FROM player_quest_instances;
