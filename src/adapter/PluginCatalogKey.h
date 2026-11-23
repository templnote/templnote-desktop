#pragma once

// Public key used to verify signed plugin packages from the store. This is the
// companion of the private key held by the store CI (never shipped).
namespace Templnote::Adapter {
inline constexpr const char *kPluginCatalogPublicKey = R"KEY(
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAbcsVK6eMcyOcQ2U/RWrjcHL6xsDuQ/3DHa3rZto129g=
-----END PUBLIC KEY-----
)KEY";
} // namespace Templnote::Adapter
