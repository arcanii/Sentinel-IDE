// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include "core/Settings.h"

namespace sentinelide {

// Show the modal Settings dialog. Returns true if the user clicked OK (in which
// case `s` has been updated); false on Cancel/close. `resolvedSnc`/`resolvedVcvars`
// are the currently-active auto-detected paths, shown as hints for the blank fields.
bool showSettingsDialog(HWND owner, Settings& s,
                        const std::wstring& resolvedSnc = L"", const std::wstring& resolvedVcvars = L"");

}  // namespace sentinelide
