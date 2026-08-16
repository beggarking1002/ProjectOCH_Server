#include "pch.h"
#include "GoogleAuthService.h"
#include "DatabaseManager.h"

#include <chrono>
#include <fstream>
#include <regex>
#include <sstream>
#include <wincrypt.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

namespace
{
	constexpr const char* kGoogleAuthConfigPath = "C:\\ProjectOCH\\Server\\Data\\GoogleAuth.json";

	bool ReadAllText(const string& path, string& outText)
	{
		ifstream file(path);
		if (!file.is_open())
			return false;
		stringstream buffer;
		buffer << file.rdbuf();
		outText = buffer.str();
		return true;
	}

	bool ExtractString(const string& json, const string& key, string& outValue)
	{
		const regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
		smatch match;
		if (!regex_search(json, match, pattern))
			return false;
		outValue = match[1].str();
		return true;
	}

	bool ExtractBool(const string& json, const string& key, bool& outValue)
	{
		const regex pattern("\\\"" + key + "\\\"\\s*:\\s*(true|false)");
		smatch match;
		if (!regex_search(json, match, pattern))
			return false;
		outValue = match[1].str() == "true";
		return true;
	}

	string UrlEncode(const string& value)
	{
		static constexpr char hex[] = "0123456789ABCDEF";
		string encoded;
		for (unsigned char ch : value)
		{
			if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
				(ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~')
			{
				encoded.push_back(static_cast<char>(ch));
			}
			else
			{
				encoded.push_back('%');
				encoded.push_back(hex[ch >> 4]);
				encoded.push_back(hex[ch & 0x0F]);
			}
		}
		return encoded;
	}

	bool DecodeBase64Url(string value, string& outDecoded)
	{
		for (char& ch : value)
		{
			if (ch == '-') ch = '+';
			else if (ch == '_') ch = '/';
		}
		while (value.size() % 4 != 0)
			value.push_back('=');

		DWORD decodedLength = 0;
		if (!CryptStringToBinaryA(value.c_str(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64,
			nullptr, &decodedLength, nullptr, nullptr))
			return false;
		outDecoded.resize(decodedLength);
		return CryptStringToBinaryA(value.c_str(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64,
			reinterpret_cast<BYTE*>(outDecoded.data()), &decodedLength, nullptr, nullptr) != FALSE;
	}

	bool IsAllowedLoopbackRedirect(const string& redirectUri)
	{
		return redirectUri.starts_with("http://127.0.0.1:") || redirectUri.starts_with("http://localhost:");
	}

	string SingleLine(string value)
	{
		for (char& ch : value)
		{
			if (ch == '\r' || ch == '\n' || ch == '\t')
				ch = ' ';
		}
		return value;
	}
}

GoogleAuthService GGoogleAuth;

bool GoogleAuthService::Initialize(bool allowDevelopmentLogin)
{
	_allowDevelopmentLogin = allowDevelopmentLogin;
	ifstream configFile(kGoogleAuthConfigPath);
	if (!configFile.is_open())
	{
		cout << "[GoogleAuth] Data/GoogleAuth.json is absent. Google login is unavailable." << endl;
		cout << "[GoogleAuth] Development login " << (_allowDevelopmentLogin ? "enabled" : "disabled") << endl;
		return true;
	}
	if (!LoadConfig())
		return false;
	if (_enabled && !GDatabase.IsEnabled())
	{
		cout << "[GoogleAuth] Google authentication requires MySQL persistence." << endl;
		return false;
	}
	cout << "[GoogleAuth] " << (_enabled ? "enabled" : "disabled") << endl;
	if (!_enabled)
		cout << "[GoogleAuth] Development login " << (_allowDevelopmentLogin ? "enabled" : "disabled") << endl;
	return true;
}

bool GoogleAuthService::LoadConfig()
{
	string json;
	if (!ReadAllText(kGoogleAuthConfigPath, json) || !ExtractBool(json, "enabled", _enabled))
	{
		cout << "[GoogleAuth] Invalid GoogleAuth.json: enabled is required." << endl;
		return false;
	}
	if (!_enabled)
		return true;
	if (!ExtractString(json, "client_id", _clientId) || !ExtractString(json, "client_secret", _clientSecret) ||
		_clientId.empty() || _clientSecret.empty())
	{
		cout << "[GoogleAuth] Invalid GoogleAuth.json: client_id and client_secret are required." << endl;
		return false;
	}
	return true;
}

bool GoogleAuthService::ExchangeAuthorizationCode(const string& authorizationCode, const string& codeVerifier,
	const string& redirectUri, GoogleIdentity& outIdentity, string& outReason) const
{
	if (!_enabled)
	{
		outReason = "google authentication is disabled";
		return false;
	}
	if (authorizationCode.empty() || authorizationCode.size() > 4096 || codeVerifier.size() < 43 ||
		codeVerifier.size() > 128 || !IsAllowedLoopbackRedirect(redirectUri))
	{
		outReason = "invalid google authorization response";
		return false;
	}

	const string body = "code=" + UrlEncode(authorizationCode) +
		"&client_id=" + UrlEncode(_clientId) +
		"&client_secret=" + UrlEncode(_clientSecret) +
		"&code_verifier=" + UrlEncode(codeVerifier) +
		"&redirect_uri=" + UrlEncode(redirectUri) +
		"&grant_type=authorization_code";
	string response;
	if (!PostTokenRequest(body, response, outReason))
		return false;

	string idToken;
	if (!ExtractString(response, "id_token", idToken))
	{
		outReason = "google token response did not contain an id token";
		return false;
	}
	return ParseAndValidateIdToken(idToken, outIdentity, outReason);
}

bool GoogleAuthService::PostTokenRequest(const string& body, string& outResponse, string& outReason) const
{
	HINTERNET session = WinHttpOpen(L"ProjectOCH-GameServer/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == nullptr)
	{
		outReason = "failed to initialize HTTPS client";
		return false;
	}
	HINTERNET connection = WinHttpConnect(session, L"oauth2.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection != nullptr
		? WinHttpOpenRequest(connection, L"POST", L"/token", nullptr, WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
		: nullptr;
	bool success = request != nullptr && WinHttpSendRequest(request,
		L"Content-Type: application/x-www-form-urlencoded\r\n", -1L,
		const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) &&
		WinHttpReceiveResponse(request, nullptr);

	DWORD statusCode = 0;
	DWORD statusSize = sizeof(statusCode);
	if (success)
		success = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX) != FALSE;
	while (success)
	{
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available))
		{
			success = false;
			break;
		}
		if (available == 0)
			break;
		const size_t oldSize = outResponse.size();
		outResponse.resize(oldSize + available);
		DWORD read = 0;
		if (!WinHttpReadData(request, outResponse.data() + oldSize, available, &read))
		{
			success = false;
			break;
		}
		outResponse.resize(oldSize + read);
	}

	if (request != nullptr) WinHttpCloseHandle(request);
	if (connection != nullptr) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	if (!success || statusCode != 200)
	{
		string googleError;
		string googleErrorDescription;
		ExtractString(outResponse, "error", googleError);
		ExtractString(outResponse, "error_description", googleErrorDescription);
		cout << "[GoogleAuth] Token exchange failed status=" << statusCode
			<< " error=" << (googleError.empty() ? "transport_or_unknown" : SingleLine(googleError));
		if (!googleErrorDescription.empty())
			cout << " description=" << SingleLine(googleErrorDescription);
		cout << endl;
		outReason = "google token exchange failed";
		return false;
	}
	return true;
}

bool GoogleAuthService::ParseAndValidateIdToken(const string& idToken, GoogleIdentity& outIdentity, string& outReason) const
{
	const size_t firstDot = idToken.find('.');
	const size_t secondDot = firstDot != string::npos ? idToken.find('.', firstDot + 1) : string::npos;
	if (firstDot == string::npos || secondDot == string::npos)
	{
		outReason = "invalid google id token";
		return false;
	}
	string payload;
	if (!DecodeBase64Url(idToken.substr(firstDot + 1, secondDot - firstDot - 1), payload))
	{
		outReason = "invalid google id token payload";
		return false;
	}
	string issuer;
	string audience;
	string expiration;
	if (!ExtractString(payload, "iss", issuer) || !ExtractString(payload, "aud", audience) ||
		!ExtractString(payload, "sub", outIdentity.subject))
	{
		outReason = "google id token is missing required claims";
		return false;
	}
	const regex expirationPattern("\\\"exp\\\"\\s*:\\s*(\\d+)");
	const regex emailVerifiedPattern("\\\"email_verified\\\"\\s*:\\s*true");
	if (!regex_search(payload, expirationPattern) || !regex_search(payload, emailVerifiedPattern))
	{
		outReason = "google account email is not verified";
		return false;
	}
	if ((issuer != "https://accounts.google.com" && issuer != "accounts.google.com") || audience != _clientId)
	{
		outReason = "google id token audience or issuer mismatch";
		return false;
	}
	smatch expirationMatch;
	regex_search(payload, expirationMatch, expirationPattern);
	const int64 expiresAt = stoll(expirationMatch[1].str());
	const int64 now = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
	if (expiresAt <= now || outIdentity.subject.empty())
	{
		outReason = "google id token expired";
		return false;
	}
	ExtractString(payload, "email", outIdentity.email);
	ExtractString(payload, "name", outIdentity.displayName);
	if (outIdentity.displayName.empty())
		outIdentity.displayName = "Player";
	return true;
}
