#pragma once

#pragma warning(push)
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include "Plugin.h"
#include "SKSE/Logger.h"

#define SKSE_LOG_TRACE(...) REX::TRACE(__VA_ARGS__)
#define SKSE_LOG_DEBUG(...) REX::DEBUG(__VA_ARGS__)
#define SKSE_LOG_INFO(...) REX::INFO(__VA_ARGS__)
#define SKSE_LOG_WARN(...) REX::WARN(__VA_ARGS__)
#define SKSE_LOG_ERROR(...) REX::ERROR(__VA_ARGS__)
#define SKSE_LOG_CRITICAL(...) REX::CRITICAL(__VA_ARGS__)
#define SKSE_LOG_FAIL(...) REX::FAIL(__VA_ARGS__)