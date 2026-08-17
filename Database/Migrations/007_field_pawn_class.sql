-- The selected field representation is account progression and must survive
-- reconnects. PawnClass numeric values are defined by Enum.proto.
SET @field_pawn_class_exists = (
    SELECT COUNT(*)
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'player_profiles'
      AND COLUMN_NAME = 'field_pawn_class'
);

SET @field_pawn_class_sql = IF(
    @field_pawn_class_exists = 0,
    'ALTER TABLE player_profiles ADD COLUMN field_pawn_class INT UNSIGNED NOT NULL DEFAULT 4 AFTER fame',
    'SELECT 1'
);

PREPARE field_pawn_class_statement FROM @field_pawn_class_sql;
EXECUTE field_pawn_class_statement;
DEALLOCATE PREPARE field_pawn_class_statement;
