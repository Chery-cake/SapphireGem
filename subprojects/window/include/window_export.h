#ifndef WINDOW_EXPORT_H_
#define WINDOW_EXPORT_H_

// Window module export macros for shared library
#ifdef _WIN32
#ifdef WINDOW_EXPORTS
#define WINDOW_API __declspec(dllexport)
#else
#define WINDOW_API __declspec(dllimport)
#endif
#else
#ifdef WINDOW_EXPORTS
#define WINDOW_API __attribute__((visibility("default")))
#else
#define WINDOW_API
#endif
#endif

#endif // WINDOW_EXPORT_H_
