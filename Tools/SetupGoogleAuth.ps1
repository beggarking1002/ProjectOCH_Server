param(
    [Parameter(Mandatory = $true)]
    [string]$CredentialPath,

    [string]$ServerConfigPath = "C:\ProjectOCH\Server\Data\GoogleAuth.json",
    [string]$ClientConfigPath = "C:\ProjectOCH\Client\Assets\StreamingAssets\GoogleAuth.json"
)

$ErrorActionPreference = "Stop"

$resolvedCredentialPath = (Resolve-Path -LiteralPath $CredentialPath).Path
$downloadedConfig = Get-Content -LiteralPath $resolvedCredentialPath -Raw | ConvertFrom-Json
$oauthClient = if ($null -ne $downloadedConfig.installed) {
    $downloadedConfig.installed
}
elseif ($null -ne $downloadedConfig.web) {
    $downloadedConfig.web
}
else {
    throw "The downloaded file is not a Google OAuth client JSON file."
}

if ([string]::IsNullOrWhiteSpace($oauthClient.client_id) -or
    [string]::IsNullOrWhiteSpace($oauthClient.client_secret)) {
    throw "The Google OAuth client JSON does not contain client_id and client_secret."
}

$serverConfig = [ordered]@{
    enabled = $true
    client_id = $oauthClient.client_id
    client_secret = $oauthClient.client_secret
} | ConvertTo-Json

$clientConfig = [ordered]@{
    client_id = $oauthClient.client_id
} | ConvertTo-Json

$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($ServerConfigPath, $serverConfig + [Environment]::NewLine, $utf8WithoutBom)
[System.IO.File]::WriteAllText($ClientConfigPath, $clientConfig + [Environment]::NewLine, $utf8WithoutBom)

Write-Host "Google OAuth configuration created."
Write-Host "Server: client_id and client_secret"
Write-Host "Client: client_id only"
