#pragma once

#include <functional>

void registerExithandler(std::function<void ()>&& onExit);
