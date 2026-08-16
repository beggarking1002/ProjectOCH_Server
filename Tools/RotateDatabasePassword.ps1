param(
    [string]$DatabaseConfigPath = "C:\ProjectOCH\Server\Data\Database.json",
    [string]$MysqlPath = "C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe"
)

$ErrorActionPreference = "Stop"

$databaseConfig = Get-Content -LiteralPath $DatabaseConfigPath -Raw | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($databaseConfig.password)) {
    throw "Database config does not contain a password."
}

$randomBytes = [byte[]]::new(32)
[System.Security.Cryptography.RandomNumberGenerator]::Fill($randomBytes)
$newPassword = [Convert]::ToBase64String($randomBytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')

$escapedUser = $databaseConfig.user.Replace("'", "''")
$alterPasswordSql = "ALTER USER '$escapedUser'@'127.0.0.1' IDENTIFIED BY '$newPassword';"

$env:MYSQL_PWD = $databaseConfig.password
try {
    $alterPasswordSql | & $MysqlPath `
        --host=$($databaseConfig.host) `
        --port=$($databaseConfig.port) `
        --user=$($databaseConfig.user) `
        --database=$($databaseConfig.database) `
        --batch

    if ($LASTEXITCODE -ne 0) {
        throw "MySQL rejected the password rotation."
    }
}
finally {
    Remove-Item -LiteralPath "Env:\MYSQL_PWD" -ErrorAction SilentlyContinue
}

$databaseConfig.password = $newPassword
$updatedConfig = $databaseConfig | ConvertTo-Json
$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($DatabaseConfigPath, $updatedConfig + [Environment]::NewLine, $utf8WithoutBom)

$env:MYSQL_PWD = $newPassword
try {
    & $MysqlPath `
        --host=$($databaseConfig.host) `
        --port=$($databaseConfig.port) `
        --user=$($databaseConfig.user) `
        --database=$($databaseConfig.database) `
        --batch `
        --skip-column-names `
        --execute="SELECT CURRENT_USER();" | Out-Null

    if ($LASTEXITCODE -ne 0) {
        throw "The rotated password could not be verified."
    }
}
finally {
    Remove-Item -LiteralPath "Env:\MYSQL_PWD" -ErrorAction SilentlyContinue
}

Write-Host "Database password rotated and verified."
