#pragma once
#include "../compiler/compiler_warnings_control.h"

#include <functional>
#include <sstream>

class AdvancedAssert
{
public:
	static void setLoggingFunc(const std::function<void (const char*)>& func);

	inline static void logAssertion(const char* condition, const char* func, int line) {
		if (_loggingFunc)
		{
			std::ostringstream stream;
			stream << "Assertion failed at " << func << ", line " << line << ": " << condition;
			_loggingFunc(stream.str().c_str());
		}
	}

	inline static void logMessage(const std::string& message, const char* func, int line) {
		if (!_loggingFunc)
			return;

		std::ostringstream stream;
		stream << func << ", line " << line << ": " << message;
		_loggingFunc(stream.str().c_str());
	}

	inline static void logMessage(const char* message, const char* func, int line) {
		logMessage(std::string(message), func, line);
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
#define assert_without_abort(x) if (::debuggerIsAttached() && !(x)) ::raise(SIGTRAP)
#endif

#if defined _DEBUG || !defined NDEBUG
#define assert_debug_only(condition) DISABLE_COMPILER_WARNINGS assert_without_abort(condition); RESTORE_COMPILER_WARNINGS
#else
#define assert_debug_only(condition)
#endif

// This macro checks the condition and prints failed assertions to the log both in debug and release. Actual assert is only triggered in debug.
#define assert_r(condition) {if (!(condition)) {AdvancedAssert::logAssertion(#condition, __FUNCTION__, __LINE__); assert_debug_only(#condition == nullptr);}}
#define assert_message_r(condition, message) {if (!(condition)) {AdvancedAssert::logMessage(message, __FUNCTION__, __LINE__); assert_debug_only(#condition == nullptr);}}
#define assert_unconditional_r(message) {AdvancedAssert::logMessage(message, __FUNCTION__, __LINE__); assert_debug_only(false);}
#define assert_and_return_r(condition, returnValue) {if (!(condition)) {AdvancedAssert::logAssertion(#condition, __FUNCTION__, __LINE__); assert_debug_only(#condition == nullptr); return returnValue;}}
#define assert_and_return_unconditional_r(message, returnValue) {AdvancedAssert::logMessage(message, __FUNCTION__, __LINE__); assert_debug_only(false); return returnValue;}
#define assert_and_return_message_r(condition, message, returnValue) {if (!(condition)) {AdvancedAssert::logMessage(message, __FUNCTION__, __LINE__); assert_debug_only(#condition == nullptr); return returnValue;}}
