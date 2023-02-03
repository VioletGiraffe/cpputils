#pragma once

#include <string>

void setThreadName(const char* asciiName);
inline void setThreadName(const std::string& str)
{
	setThreadName(str.c_str());
}

template <class Container>
void joinAll(Container& container)
{
	for (auto& t : container)
		t.join();
}
