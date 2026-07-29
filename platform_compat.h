#pragma once

#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>

#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif

#ifndef SW_SHOWNORMAL
#define SW_SHOWNORMAL 1
#endif

typedef uint64_t __int64;
typedef uint32_t DWORD;
typedef void* HANDLE;

// Cross-platform MSVC Annex K function wrappers for Linux
#ifndef swprintf_s
template<typename... Args>
inline int swprintf_s(wchar_t* buf, size_t sizeInWords, const wchar_t* format, Args... args) {
    return swprintf(buf, sizeInWords, format, args...);
}
template<typename... Args>
inline int swprintf_s(wchar_t* buf, const wchar_t* format, Args... args) {
    return swprintf(buf, 260, format, args...);
}
#endif

#ifndef sprintf_s
template<typename... Args>
inline int sprintf_s(char* buf, size_t sizeInBytes, const char* format, Args... args) {
    return snprintf(buf, sizeInBytes, format, args...);
}
template<typename... Args>
inline int sprintf_s(char* buf, const char* format, Args... args) {
    return snprintf(buf, 260, format, args...);
}
#endif

#ifndef swscanf_s
#define swscanf_s swscanf
#endif

#ifndef _wfopen_s
inline int _wfopen_s(FILE** pFile, const wchar_t* filename, const wchar_t* mode) {
    char pathNarrow[512];
    char modeNarrow[16];
    wcstombs(pathNarrow, filename, sizeof(pathNarrow));
    wcstombs(modeNarrow, mode, sizeof(modeNarrow));
    *pFile = fopen(pathNarrow, modeNarrow);
    return (*pFile != nullptr) ? 0 : -1;
}
#endif

#ifndef strcpy_s
inline int strcpy_s(char* dest, size_t destSize, const char* src) {
    strncpy(dest, src, destSize);
    dest[destSize - 1] = '\0';
    return 0;
}
template<size_t N>
inline int strcpy_s(char (&dest)[N], const char* src) {
    strncpy(dest, src, N);
    dest[N - 1] = '\0';
    return 0;
}
#endif

#ifndef localtime_s
inline int localtime_s(struct tm* _tm, const time_t* _time) {
    return (localtime_r(_time, _tm) != nullptr) ? 0 : -1;
}
#endif

#endif
