# MySQL local setup

1. Create a local database and a server-only account. Replace the placeholder
   password before running these commands.

```sql
CREATE DATABASE project_och CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
CREATE USER 'och_server'@'127.0.0.1' IDENTIFIED BY 'REPLACE_WITH_A_LONG_LOCAL_SECRET';
GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES ON project_och.* TO 'och_server'@'127.0.0.1';
FLUSH PRIVILEGES;
```

2. Copy `Data/Database.json.example` to `Data/Database.json`, then insert the
   same password. `Data/Database.json` is intentionally ignored by Git.

3. Start the server. It connects first and creates the tables from
   `001_initial_schema.sql` if they do not exist. The SQL file is also provided
   for manual database provisioning/review.

Development login is opt-in. Start the server with `-developmentLogin`, then
start each client with `-developmentLogin` and a different non-zero
`-playerIndex` (for example `-playerIndex 1` and `-playerIndex 2`). When
`-playerIndex` is omitted, the server assigns a runtime-only object ID.
Development login is not an authenticated account and is not secure enough for
a public build.

## Google OAuth

Create a Google Cloud OAuth 2.0 client of type **Desktop app**. Copy
`Data/GoogleAuth.json.example` to `Data/GoogleAuth.json` and add that client's
ID and secret. Copy the client-side example to
`Client/Assets/StreamingAssets/GoogleAuth.json` and add the same client ID.

When both Google and database configs are enabled, `C_ENTER_GAME.playerIndex`
is ignored and the verified Google account's internal `account_id` is used.
Without `GoogleAuth.json`, normal clients do not enter the field. The explicit
`-developmentLogin` option is required on both server and client for local
testing.

Use two different Google accounts to verify account separation. They must
receive different `S_LOGIN.account_id` values. Logging in twice with the same
Google account returns `this account is already connected`; reconnecting after
the first client disconnects returns the same persistent `account_id`.
