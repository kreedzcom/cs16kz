#ifndef PLATFORM_H
#define PLATFORM_H

#pragma once

#ifndef likely
#	ifndef _MSC_VER
#		define likely(x) __builtin_expect(!!(x), 1)
#	else
#		define likely(x) (x)
#	endif // _MSC_VER
#endif // likely

#ifndef unlikely
#	ifndef _MSC_VER
#		define unlikely(x) __builtin_expect(!!(x), 0)
#	else // _MSC_VER
#		define unlikely(x) (x)
#	endif // _MSC_VER
#endif // unlikely

#endif
