// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>

namespace sentinelide {

// Show the modal Signing & Trust panel (ADR 0061): a trust-manifest viewer plus
// real `snc keygen` / `sign` / `verify` over `filePath`. `sncSigns` is whether the
// active snc exposes those subcommands (else the actions are disabled with a note).
// `dir` is the project/folder root; `trustPath` is the consumer `sentinel-trust.toml`.
void showSigningDialog(HWND owner, const std::wstring& sncPath, bool sncSigns,
                       const std::wstring& filePath, const std::wstring& dir,
                       const std::wstring& trustPath);

}  // namespace sentinelide
