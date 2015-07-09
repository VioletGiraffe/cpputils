#include "advanced_assert.h"

#if defined _DEBUG == (defined NDEBUG || NDEBUG == 1)
#error "Either _DEBUG or NDEBUG=1 must be defined"
#endif

std::function<void (const char*)> AdvancedAssert::_loggingFunc;

void AdvancedAssert::setLoggingFunc(const std::function<void (const char*)>& func)
{
	_loggingFunc = func;
}
