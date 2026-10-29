#include "shellcontextmenu.hpp"

#ifdef _WIN32
#include "win_utils.hpp"
#include "../assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"

#include <CommCtrl.h>
#include <ShlObj.h>
#include <shellapi.h> // SEE_MASK_UNICODE, which CMIC_MASK_UNICODE expands to
#include <wrl/client.h>

#include <memory>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT FIRST_COMMAND_ID = 1;
constexpr UINT LAST_COMMAND_ID = 0x7FFF;

// Lives until the picked verb has run: Microsoft's hosting examples destroy the menu only after InvokeCommand.
struct ShellMenu
{
	~ShellMenu()
	{
		if (popup)
			DestroyMenu(popup);
	}

	// Declared first, so COM is uninitialized only after the menu is released.
	CoInitHelper com{ COINIT_APARTMENTTHREADED };
	ComPtr<IContextMenu> menu;
	HMENU popup = nullptr;
};

// Passes menu messages on to the shell, which fills and draws submenus such as Send to and Open with.
LRESULT CALLBACK forwardMenuMessages(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam, UINT_PTR, const DWORD_PTR contextMenu)
{
	if (message == WM_INITMENUPOPUP || message == WM_DRAWITEM || message == WM_MEASUREITEM || message == WM_MENUCHAR)
	{
		auto* const menu = reinterpret_cast<IContextMenu*>(contextMenu);
		if (ComPtr<IContextMenu3> menu3; SUCCEEDED(menu->QueryInterface(IID_PPV_ARGS(&menu3))))
		{
			if (LRESULT result = 0; SUCCEEDED(menu3->HandleMenuMsg2(message, wParam, lParam, &result)))
				return result;
		}
		else if (ComPtr<IContextMenu2> menu2; message != WM_MENUCHAR && SUCCEEDED(menu->QueryInterface(IID_PPV_ARGS(&menu2))))
		{
			if (SUCCEEDED(menu2->HandleMenuMsg(message, wParam, lParam)))
				return 0;
		}
	}
	return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

std::function<void()> pickShellContextMenuVerb(const std::vector<std::filesystem::path>& paths, const HWND owner, const RECT& anchor)
{
	assert_and_return_r(!paths.empty(), {});

	auto shellMenu = std::make_shared<ShellMenu>();
	if (!shellMenu->com.success())
		return {};

	std::vector<PIDLIST_ABSOLUTE> items;
	EXEC_ON_SCOPE_EXIT([&items] {
		for (const PIDLIST_ABSOLUTE item : items)
			ILFree(item);
	});
	items.reserve(paths.size());
	for (const std::filesystem::path& path : paths)
	{
		assert_r(path.parent_path() == paths.front().parent_path());
		PIDLIST_ABSOLUTE item = nullptr;
		if (FAILED(SHParseDisplayName(std::filesystem::path{ path }.make_preferred().c_str(), nullptr, &item, 0, nullptr)))
			return {};
		items.push_back(item);
	}

	ComPtr<IShellFolder> folder;
	if (FAILED(SHBindToParent(items.front(), IID_PPV_ARGS(&folder), nullptr)))
		return {};

	std::vector<PCUITEMID_CHILD> children;
	children.reserve(items.size());
	for (const PIDLIST_ABSOLUTE item : items)
		children.push_back(ILFindLastID(item));
	if (FAILED(folder->GetUIObjectOf(owner, static_cast<UINT>(children.size()), children.data(), __uuidof(IContextMenu), nullptr, reinterpret_cast<void**>(shellMenu->menu.GetAddressOf()))))
		return {};

	shellMenu->popup = CreatePopupMenu();
	assert_and_return_r(shellMenu->popup, {});

	// Shift adds the extended verbs, as in Explorer.
	// No CMF_CANRENAME: renaming needs an Explorer view to edit the name in.
	const UINT flags = GetKeyState(VK_SHIFT) < 0 ? CMF_EXTENDEDVERBS : CMF_NORMAL;
	if (FAILED(shellMenu->menu->QueryContextMenu(shellMenu->popup, 0, FIRST_COMMAND_ID, LAST_COMMAND_ID, flags)))
		return {};

	SetWindowSubclass(owner, forwardMenuMessages, 0, reinterpret_cast<DWORD_PTR>(shellMenu->menu.Get()));
	TPMPARAMS placement{ sizeof(TPMPARAMS), anchor };
	const auto command = static_cast<UINT>(TrackPopupMenuEx(shellMenu->popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, anchor.right, anchor.top, owner, &placement));
	RemoveWindowSubclass(owner, forwardMenuMessages, 0);
	if (command < FIRST_COMMAND_ID)
		return {};

	CMINVOKECOMMANDINFOEX invocation{};
	invocation.cbSize = sizeof(invocation);
	invocation.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
	if (GetKeyState(VK_SHIFT) < 0)
		invocation.fMask |= CMIC_MASK_SHIFT_DOWN;
	if (GetKeyState(VK_CONTROL) < 0)
		invocation.fMask |= CMIC_MASK_CONTROL_DOWN;
	invocation.hwnd = owner;
	invocation.lpVerb = MAKEINTRESOURCEA(command - FIRST_COMMAND_ID);
	invocation.lpVerbW = MAKEINTRESOURCEW(command - FIRST_COMMAND_ID);
	invocation.nShow = SW_SHOWNORMAL;
	GetCursorPos(&invocation.ptInvoke);

	return [shellMenu, invocation]() mutable {
		shellMenu->menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&invocation));
	};
}
#endif
