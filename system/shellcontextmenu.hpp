#pragma once

#ifdef _WIN32
#include <Windows.h>

#include <filesystem>
#include <functional>
#include <vector>

// Shows Explorer's context menu for paths, which must all be in one folder, and blocks until the menu closes.
// The menu opens next to anchor (in screen pixels), placed the way Windows places a submenu.
// owner gets the menu's window messages and is the parent of any dialog the verb shows.
// Returns the picked verb for the caller to run on this thread; empty when cancelled or the shell has no menu for paths.
[[nodiscard]] std::function<void()> pickShellContextMenuVerb(const std::vector<std::filesystem::path>& paths, HWND owner, const RECT& anchor);
#endif
