#include "win_utils.hpp"

#include <Windows.h>
#include <comdef.h>

#include <string_view>

static DWORD formatSystemMessage(const DWORD errCode, const DWORD languageId, wchar_t (&buffer)[2048]) noexcept
{
	return ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, errCode, languageId, buffer, static_cast<DWORD>(std::size(buffer)), nullptr);
}

std::string ErrorStringFromErrorCode(const DWORD errCode) noexcept
{
	wchar_t msgBuf[2048];
	// Language 0 is the system's lookup order: a localized Windows may have no English messages installed
	DWORD nCharsWritten = formatSystemMessage(errCode, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), msgBuf);
	if (nCharsWritten == 0)
		nCharsWritten = formatSystemMessage(errCode, 0, msgBuf);
	if (nCharsWritten == 0)
		return {};

	std::wstring_view message{ msgBuf, nCharsWritten };
	if (message.ends_with(L"\r\n"))
		message.remove_suffix(2);

	const int messageLength = static_cast<int>(message.size());
	std::string str(static_cast<size_t>(::WideCharToMultiByte(CP_UTF8, 0, message.data(), messageLength, nullptr, 0, nullptr, nullptr)), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, message.data(), messageLength, str.data(), static_cast<int>(str.size()), nullptr, nullptr);
	return str;
}

std::string ErrorStringFromLastError() noexcept
{
	return ErrorStringFromErrorCode(::GetLastError());
}

std::string ErrorStringFromHRESULT(HRESULT hr) noexcept
{
	return ErrorStringFromErrorCode(hr);
}
