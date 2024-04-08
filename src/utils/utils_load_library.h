/*
 *
 * Copyright (C) 2024 Intel Corporation
 *
 * Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 */

/*
 * Including this header forces linking with libdl on Linux.
 */
#define _GNU_SOURCE 1

#ifndef UMF_LOAD_LIBRARY_H
#define UMF_LOAD_LIBRARY_H 1

#define UMF_UTIL_OPEN_LIBRARY_GLOBAL 1

#ifdef _WIN32 /* Windows */

#include <windows.h>

#include <libloaderapi.h>

#else /* Linux */

#include <dlfcn.h> // forces linking with libdl on Linux

#endif /* _WIN32 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32 /* Windows */

static inline void *util_open_library(const char *filename, int userFlags) {
    (void)userFlags; //unused for win
    return LoadLibrary(TEXT(filename));
}

static inline int util_close_library(void *handle) {
    // If the FreeLibrary function succeeds, the return value is nonzero.
    // If the FreeLibrary function fails, the return value is zero.
    return (FreeLibrary((HMODULE)handle) == 0);
}

static inline void *util_get_symbol_addr(void *handle, const char *symbol,
                                         const char *libname) {
    if (!handle) {
        if (libname == NULL) {
            return NULL;
        }
        handle = GetModuleHandle(libname);
    }
    return GetProcAddress((HMODULE)handle, symbol);
}

#else /* Linux */

static inline void *util_open_library(const char *filename, int userFlags) {
    int dlopenFlags = RTLD_LAZY;
    if (userFlags & UMF_UTIL_OPEN_LIBRARY_GLOBAL) {
        dlopenFlags |= RTLD_GLOBAL;
    }
    return dlopen(filename, dlopenFlags);
}

static inline int util_close_library(void *handle) { return dlclose(handle); }

static inline void *util_get_symbol_addr(void *handle, const char *symbol,
                                         const char *libname) {
    (void)libname; //unused
    if (!handle) {
        handle = RTLD_DEFAULT;
    }
    return dlsym(handle, symbol);
}

#endif /* _WIN32 */

#ifdef __cplusplus
}
#endif

#endif /* UMF_LOAD_LIBRARY_H */
