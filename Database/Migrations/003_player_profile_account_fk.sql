-- Player profiles use the authenticated account id as their permanent player
-- id. Make that one-to-one relationship explicit so deleting an account also
-- removes all profile-owned progress through the existing cascade chain.
--
-- MySQL does not support ADD CONSTRAINT IF NOT EXISTS. Dynamic SQL keeps this
-- migration safe to retry if the DDL committed but migration history did not.

SET @player_profile_account_fk_exists = (
    SELECT COUNT(*)
    FROM information_schema.REFERENTIAL_CONSTRAINTS
    WHERE CONSTRAINT_SCHEMA = DATABASE()
      AND TABLE_NAME = 'player_profiles'
      AND CONSTRAINT_NAME = 'fk_player_profile_account'
);

SET @player_profile_account_fk_sql = IF(
    @player_profile_account_fk_exists = 0,
    'ALTER TABLE player_profiles ADD CONSTRAINT fk_player_profile_account FOREIGN KEY (player_id) REFERENCES accounts(account_id) ON DELETE CASCADE',
    'SELECT 1'
);

PREPARE player_profile_account_fk_statement FROM @player_profile_account_fk_sql;
EXECUTE player_profile_account_fk_statement;
DEALLOCATE PREPARE player_profile_account_fk_statement;
