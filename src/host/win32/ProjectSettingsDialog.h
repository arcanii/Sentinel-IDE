// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include "core/Project.h"

namespace sentinelide {

// Show the modal Project Settings dialog — a structured form over `sentinel.toml`.
// Returns true if the user clicked Save (in which case `p` has been updated and the
// caller should persist it via saveProject); false on Cancel/close.
bool showProjectSettingsDialog(HWND owner, SentinelProject& p);

}  // namespace sentinelide
