// src/Support/Log.h
#pragma once

#include <REX/LOG.h>

// REX::DEBUG / REX::TRACE 在 Release 里仍会求值参数、执行 std::vformat，
// 只是 spdlog 运行时 level 过滤掉不写盘。SPDLOG_ACTIVE_LEVEL 对 REX 无效
// （REX/LOG.h 没有编译期守卫）。这里用项目宏做真正的零开销剔除：
//   普通版：LOG_DEBUG(...) → ((void)0)
//   详细版：LOG_DEBUG(...) → REX::DEBUG(...)，sink / 路径 / 格式全部走 REX
// INFO / WARN / CRITICAL / FAIL 不包宏，继续直接写 REX::。
#ifdef ADEPTIVE_PANTYHOSE_DETAILED_LOG
#define LOG_TRACE(...) REX::TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) REX::DEBUG(__VA_ARGS__)
#else
#define LOG_TRACE(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#endif
