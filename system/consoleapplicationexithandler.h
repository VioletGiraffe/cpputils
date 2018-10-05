#pragma once

#include <functional>

void registerExithandler(std::function<bool()>&& onExit);
