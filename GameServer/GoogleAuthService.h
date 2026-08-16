#pragma once

struct GoogleIdentity
{
	string subject;
	string email;
	string displayName;
};

class GoogleAuthService
{
public:
	bool Initialize(bool allowDevelopmentLogin);
	bool IsEnabled() const { return _enabled; }
	bool IsDevelopmentLoginAllowed() const { return _allowDevelopmentLogin; }
	bool ExchangeAuthorizationCode(const string& authorizationCode, const string& codeVerifier,
		const string& redirectUri, GoogleIdentity& outIdentity, string& outReason) const;

private:
	bool LoadConfig();
	bool PostTokenRequest(const string& body, string& outResponse, string& outReason) const;
	bool ParseAndValidateIdToken(const string& idToken, GoogleIdentity& outIdentity, string& outReason) const;

private:
	bool _enabled = false;
	bool _allowDevelopmentLogin = false;
	string _clientId;
	string _clientSecret;
};

extern GoogleAuthService GGoogleAuth;
