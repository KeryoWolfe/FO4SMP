#pragma once
#include <cstring>
#include <smmintrin.h>
#include <tbb/tbb.h>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <mutex>
#include <string>
#include <format>
#define _CRT_ALIGN(x) alignas(x)
#define __forceinline inline
#define ZeroMemory(d, l) std::memset((d), 0, (l))
using UINT = unsigned int;
using BYTE = unsigned char;
namespace logger { template<class... A> void info(A&&...){} template<class... A> void warn(A&&...){} template<class... A> void error(A&&...){} template<class... A> void debug(A&&...){} }
