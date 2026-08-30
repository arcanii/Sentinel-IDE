// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>

namespace sentinelide {

// What the user chose when asked about an unsaved file.
enum class SaveChoice { Save, Discard, Cancel };

// Modal, dark-themed "Save changes to <file>?" prompt (matches the other dialogs
// rather than a light MessageBox — see phase 15). `action` names what is about to
// happen in the secondary line, e.g. L"closing" / L"opening another file".
// Esc / the close box return Cancel, so the caller must treat Cancel as "abort".
SaveChoice showSaveChangesDialog(HWND owner, const std::wstring& fileName, const std::wstring& action);

}  // namespace sentinelide
