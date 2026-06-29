// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>

namespace sentinelide {

// Modal, dark-themed password prompt. With confirm=true a second field must match
// (used when sealing). Returns true and fills `out` on OK; false on Cancel.
bool showPasswordDialog(HWND owner, const std::wstring& title, const std::wstring& prompt, bool confirm, std::wstring& out);

}  // namespace sentinelide
