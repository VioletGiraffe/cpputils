#include "advanced_assert.h"

#include "string/string_helpers.hpp"

#include <string>
#include <utility>

#if (defined _DEBUG && defined NDEBUG && NDEBUG != 0) || (!defined _DEBUG && defined NDEBUG && NDEBUG != 1)
#error "Either _DEBUG or NDEBUG=1 must be defined"
#endif

std::function<void (const char*)> AdvancedAssert::_loggingFunc;

void AdvancedAssert::setLoggingFunc(std::function<void (const char*)> func)
{
	_loggingFunc = std::move(func);
}

void AdvancedAssert::logAssertion(const char* condition, const char* func, int line)
{
	if (_loggingFunc)
	{
		std::string stream;
		stream << "Assertion failed at " << func << ", line " << line << ": " << condition;
		_loggingFunc(stream.c_str());
	}
}

void AdvancedAssert::logMessage(std::string_view message, const char* func, int line)
{
	if (!_loggingFunc)
		return;

	std::string stream;
	stream << func << ", line " << line << ": " << message;
	_loggingFunc(stream.c_str());
}
