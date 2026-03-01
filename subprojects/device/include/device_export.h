#ifndef DEVICE_EXPORT_H_
#define DEVICE_EXPORT_H_

#ifdef _WIN32
#ifdef DEVICE_EXPORTS
#define DEVICE_API __declspec(dllexport)
#else
#define DEVICE_API __declspec(dllimport)
#endif
#else
#ifdef DEVICE_EXPORTS
#define DEVICE_API __attribute__((visibility("default")))
#else
#define DEVICE_API
#endif
#endif

#endif // DEVICE_EXPORT_H_
