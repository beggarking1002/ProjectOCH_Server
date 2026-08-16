param(
    [string]$DatabaseConfigPath = "C:\ProjectOCH\Server\Data\Database.json",
    [string]$SetupSqlPath = "C:\ProjectOCH\Server\tmp\DatabaseSetup.local.sql",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

if ((Test-Path -LiteralPath $DatabaseConfigPath) -and !$Force) {
    throw "Database config already exists. Use -Force only when rotating the local database password."
}

$randomBytes = [byte[]]::new(32)
[System.Security.Cryptography.RandomNumberGenerator]::Fill($randomBytes)
$databasePassword = [Convert]::ToBase64String($randomBytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')

$databaseConfig = [ordered]@{
    enabled = $true
    host = "127.0.0.1"
    port = 3306
    user = "och_server"
    password = $databasePassword
    database = "project_och"
    charset = "utf8mb4"
    autosave_seconds = 5
    mysql_library_path = "C:\Program Files\MySQL\MySQL Server 8.0\lib\libmysql.dll"
} | ConvertTo-Json

$setupSql = @"
CREATE DATABASE IF NOT EXISTS project_och
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_0900_ai_ci;

CREATE USER IF NOT EXISTS 'och_server'@'127.0.0.1'
    IDENTIFIED BY '$databasePassword';
ALTER USER 'och_server'@'127.0.0.1'
    IDENTIFIED BY '$databasePassword';

GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES
    ON project_och.*
    TO 'och_server'@'127.0.0.1';
FLUSH PRIVILEGES;
"@

$setupDirectory = Split-Path -Parent $SetupSqlPath
[System.IO.Directory]::CreateDirectory($setupDirectory) | Out-Null

$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($DatabaseConfigPath, $databaseConfig + [Environment]::NewLine, $utf8WithoutBom)
[System.IO.File]::WriteAllText($SetupSqlPath, $setupSql, $utf8WithoutBom)

Write-Host "Local database configuration prepared."
Write-Host "Run the generated SQL as a MySQL administrator: $SetupSqlPath"
Write-Host "The generated password was written only to ignored local files."
