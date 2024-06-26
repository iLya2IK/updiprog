#ifndef SLEEP_H
#define SLEEP_H

#include <stdint.h>
#include <stdbool.h>
#ifdef __MINGW32__
#include <windows.h>
#include <unistd.h>
#endif // __MINGW32__
#if defined(__APPLE__) || defined(__linux)
#include <unistd.h>
#endif // __linux

#ifdef __cplusplus
extern "C"
{
#endif
void msleep(uint32_t usec);
#ifdef __cplusplus
}
#endif

#endif
