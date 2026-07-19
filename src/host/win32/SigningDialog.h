// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include "../../core/Signing.h"

namespace sentinelide {

// Show the modal Signing & Trust panel (ADR 0061): a trust-manifest viewer plus
// real `snc keygen` / `sign` / `verify` over `filePath`. `caps` says what the active
// snc can actually do — verify and keygen/sign are separate capabilities that fail
// independently, so each action is gated on the one it needs (a build that verifies
// but has no keygen_core/sign_core beside it keeps Verify live and greys the rest).
// `dir` is the project/folder root; `trustPath` is the consumer `sentinel-trust.toml`.
void showSigningDialog(HWND owner, const std::wstring& sncPath, SncSigningCaps caps,
                       const std::wstring& filePath, const std::wstring& dir,
                       const std::wstring& trustPath);

}  // namespace sentinelide
