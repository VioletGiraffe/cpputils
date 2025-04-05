#include "win_utils.hpp"
#include "../assert/advanced_assert.h"

#include <Windows.h>
#include <comdef.h>

std::string ErrorStringFromErrorCode(const DWORD errCode) noexcept
{
	char msgBuf[2048];
	const auto nCharsWritten = ::FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		errCode,
		MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
		msgBuf,
		static_cast<DWORD>(std::size(msgBuf)),
		nullptr);

	assert_and_return_message_r(nCharsWritten > 0, "FormatMessageA failed with error code " + std::to_string(::GetLastError()), {});

	std::string str(msgBuf, nCharsWritten);
	if (str.ends_with("\r\n"))
		str[str.size() - 2] = '\0'; // Remove trailing CRLF

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
