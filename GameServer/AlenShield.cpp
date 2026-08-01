#include "pch.h"
#include "AlenShield.h"

namespace
{
	constexpr const char* kCarbasGuardStatusKey = "ALEN_SHIELD_CARBAS_GUARD";
	constexpr const char* kResponsibilityStatusKey = "ALEN_SHIELD_RESPONSIBILITY";
}

bool AlenShield::CanInterceptSingleTargetAttack() const
{
	return HasActiveStatus(kResponsibilityStatusKey);
}

bool AlenShield::CanCounterattackOnSuccessfulHit() const
{
	return HasActiveStatus(kCarbasGuardStatusKey);
}
