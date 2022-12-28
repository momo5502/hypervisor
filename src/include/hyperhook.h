#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif
#endif

#ifndef DLL_IMPORT
#define DLL_IMPORT __declspec(dllimport)
#endif

EXTERN_C DLL_IMPORT
int hyperhook_initialize();

EXTERN_C DLL_IMPORT
int hyperhook_write(unsigned int process_id, unsigned long long address, const void* data,
                    unsigned long long size);
