#pragma once

#include "string/string_helpers.hpp"

#include <functional>
#include <string_view>

class AdvancedAssert
{
public:
	static void setLoggingFunc(const std::function<void (const char*)>& func);

	inline static void logAssertion(const char* condition, const char* func, int line) {
		if (_loggingFunc)
		{
			std::string stream;
			stream << "Assertion failed at " << func << ", line " << line << ": " << condition;
			_loggingFunc(stream.c_str());
		}
	}

	inline static void logMessage(std::string_view message, const char* func, int line) {
		if (!_loggingFunc)
			return;

		std::string stream;
		stream << func << ", line " << line << ": " << message;
		_loggingFunc(stream.c_str());
	}

private:
	static std::function<void (const char*)> _loggingFunc;
};

#ifdef _WIN32
#include <crtdbg.h>
#define assert_without_abort(x) _ASSERT(x)
#else
#include "../debugger/debugger_is_attached.h"
#include <signal.h>
#define assert_without_abort(x) if (!(x) && ::debuggerIsAttached()) [[unlikely]] ::raise(SIGTRAP)
#endif

#if defined _DEBUG || !defined NDEBUG
#define assert_debug_only(condition) assert_without_abort(condition)
#else
#define assert_debug_only(condition) (void)0 // Without this, 'if (x) assert_debug_only(y); else z;' would not be valid
#endif

// This macro checks the condition and prints failed assertions to the log both in debug and release. Actual assert is only triggered in debug.

// This implementation as an expression plays nicely with constructs like 'if (x) hlassert(y); else z;'
// and lets you terminate hlassert with ';' in the calling code without breaking logic
#define assert_r(condition) do {if (!static_cast<bool>(condition)) [[unlikely]] {AdvancedAssert::logAssertion(#condition, __FUNCTION__, __LINE__); assert_debug_only(!static_cast<bool>(#condition));}} while(false)
#define assert_message_r(condition, message) do {if (!static_cast<bool>(condition)) [[unlikely]] {AdvancedAssert::logMessage((message), __FUNCTION__, __LINE__); assert_debug_only(!static_cast<bool>(#message));}} while(false)
#define assert_unconditional_r(message) do {AdvancedAssert::logMessage((message), __FUNCTION__, __LINE__); assert_debug_only(!static_cast<bool>(#message));} while(false)

#define assert_and_return_r(condition, returnValue) do {if (!static_cast<bool>(condition)) [[unlikely]] {AdvancedAssert::logAssertion(#condition, __FUNCTION__, __LINE__); assert_debug_only(!static_cast<bool>(#condition)); return returnValue;}} while(false)
#define assert_and_return_message_r(condition, message, returnValue) do {if (!static_cast<bool>(condition)) [[unlikely]] {AdvancedAssert::logMessage((message), __FUNCTION__, __LINE__); assert_debug_only(!static_cast<bool>(#message)); return returnValue;}} while(false)
#define assert_and_return_unconditional_r(message, returnValue) do {AdvancedAssert::logMessage((message), __FUNCTION__, __LINE__); assert_debug_only(!static_cast<bool>(#message)); return returnValue;} while(false)
