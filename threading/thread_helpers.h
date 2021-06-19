#pragma once

#include <string>

void setThreadName(const char* asciiName);
inline void setThreadName(const std::string& str)
{
	setThreadName(str.c_str());
}
