// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>

namespace sentinelide {

// What the user chose when told the open file changed underneath their edits.
enum class ReloadChoice {
    Keep,    // leave the buffer alone -- the next save overwrites the disk version
    Reload,  // discard the unsaved edits and take what is on disk
};

// Modal, dark-themed "<file> changed on disk" prompt. Asked ONLY when the buffer is
// dirty: with a clean buffer there is nothing to weigh up and the caller reloads
// silently, so every appearance of this dialog is a real either/or.
//
// KEEP IS THE DEFAULT, and Esc / the close box return it. Reload throws away typing
// that exists nowhere else and that no undo can bring back (a reload resets the undo
// buffer), whereas Keep only risks a later, separately-initiated save. Between an
// irreversible answer and a reversible one, the stray Enter must land on the
// reversible one -- the same reasoning v0.1.12 applied to the update prompt.
ReloadChoice showReloadDialog(HWND owner, const std::wstring& fileName);

}  // namespace sentinelide
