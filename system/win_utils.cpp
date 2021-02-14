#include "win_utils.hpp"
#include "../assert/advanced_assert.h"

#include <Windows.h>

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
		std::size(msgBuf),
		nullptr);

	assert_and_return_message_r(nCharsWritten > 0, "FormatMessageA failed with error code " + std::to_string(::GetLastError()), {});

	return std::string(msgBuf, nCharsWritten);
}

std::string ErrorStringFromLastError() noexcept
{
	return ErrorStringFromErrorCode(::GetLastError());
}
