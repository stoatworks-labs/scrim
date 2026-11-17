#pragma once

/// What a host reads: the four-byte id and the sixteen-byte display name.
/// One place, so the registration, the harness's --names and verify.sh's
/// oxbow probe all say the same thing.
namespace scrim::names
{
inline constexpr const char* kMixerId   = "SM01";
inline constexpr const char* kMixerName = "SW Scrim";
} // namespace scrim::names
