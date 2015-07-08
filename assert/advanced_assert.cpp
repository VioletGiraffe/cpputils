#include "advanced_assert.h"

std::function<void (const char*)> AdvancedAssert::_loggingFunc;

void AdvancedAssert::setLoggingFunc(const std::function<void (const char*)>& func)
{

}
