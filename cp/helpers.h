// FaceGate CP - helpers modelled on Microsoft's SampleV2CredentialProvider (MIT).
#pragma once
#include "common.h"

namespace fgcp {

HRESULT field_descriptor_copy(const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR& src, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** out);
HRESULT negotiate_auth_package(ULONG* pkg);

// Build the logon serialization for `qualified_user` (e.g. "MicrosoftAccount\\me@x.com" or "PC\\user").
// Local accounts use KERB_INTERACTIVE_UNLOCK_LOGON (works for unlock); others use CredPackAuthenticationBuffer.
HRESULT build_serialization(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, bool local_user, const std::wstring& qualified_user,
                            const std::wstring& password, CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* out);

}  // namespace fgcp
