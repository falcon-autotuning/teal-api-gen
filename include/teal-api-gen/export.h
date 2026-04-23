#ifndef TEAL_API_GEN_EXPORT_H
#define TEAL_API_GEN_EXPORT_H

#ifdef _WIN32
// Building the DLL
#ifdef teal_api_EXPORTS
#define TEAL_API_GEN_API __declspec(dllexport)
#else
// Using the DLL
#define TEAL_API_GEN_API __declspec(dllimport)
#endif
#else
// Unix/Linux - use visibility attributes for better control
#if defined(__GNUC__) || defined(__clang__)
#define TEAL_API_GEN_API __attribute__((visibility("default")))
#else
#define TEAL_API_GEN_API
#endif
#endif

#endif // TEAL_API_GEN_EXPORT_H
