#pragma once

#ifdef _WIN32
#include "../assert/advanced_assert.h"

#include <Objbase.h>

#include <string>

class CoInitHelper {
public:
	inline explicit CoInitHelper(COINIT threadingMode) {

		const auto hr = ::CoInitializeEx(nullptr, threadingMode);
		_initializationSucceeded = (hr == S_OK || hr == S_FALSE);
		assert_r(_initializationSucceeded);
	}

	inline ~CoInitHelper() {
		if (_initializationSucceeded)
			::CoUninitialize();

	}
	inline bool success() const {
		return _initializationSucceeded;
	}

private:
	bool _initializationSucceeded = false;
};

[[nodiscard]] std::string ErrorStringFromErrorCode(DWORD errCode) noexcept;
[[nodiscard]] std::string ErrorStringFromLastError() noexcept;

#define CO_INIT_HELPER(threadingMode) CoInitHelper _co_init_helper{threadingMode}

#else

#define CO_INIT_HELPER(threadingMode)

#endif
