/*
 * Copyright (C) 2023 Intel Corporation
 *
 * Under the Apache License v2.0 with LLVM Exceptions. See LICENSE.TXT.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "provider_os_memory_internal.h"
#include <umf/providers/provider_os_memory.h>

umf_result_t os_translate_mem_protection_one_flag(unsigned in_protection,
                                                  unsigned *out_protection) {
    switch (in_protection) {
    case UMF_PROTECTION_NONE:
        *out_protection = PROT_NONE;
        return UMF_RESULT_SUCCESS;
    case UMF_PROTECTION_READ:
        *out_protection = PROT_READ;
        return UMF_RESULT_SUCCESS;
    case UMF_PROTECTION_WRITE:
        *out_protection = PROT_WRITE;
        return UMF_RESULT_SUCCESS;
    case UMF_PROTECTION_EXEC:
        *out_protection = PROT_EXEC;
        return UMF_RESULT_SUCCESS;
    }
    return UMF_RESULT_ERROR_INVALID_ARGUMENT;
}

umf_result_t os_translate_mem_protection_flags(unsigned in_protection,
                                               unsigned *out_protection) {
    // translate protection - combination of 'umf_mem_protection_flags_t' flags
    return os_translate_flags(in_protection, UMF_PROTECTION_MAX,
                              os_translate_mem_protection_one_flag,
                              out_protection);
}

static int os_translate_purge_advise(umf_purge_advise_t advise) {
    switch (advise) {
    case UMF_PURGE_LAZY:
        return MADV_FREE;
    case UMF_PURGE_FORCE:
        return MADV_DONTNEED;
    }
    assert(0);
    return -1;
}

void *os_mmap(void *hint_addr, size_t length, int prot) {
    // MAP_ANONYMOUS - the mapping is not backed by any file
    void *ptr =
        mmap(hint_addr, length, prot, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }
    return ptr;
}

int os_munmap(void *addr, size_t length) { return munmap(addr, length); }

size_t os_get_page_size(void) { return sysconf(_SC_PAGE_SIZE); }

int os_purge(void *addr, size_t length, int advice) {
    return madvise(addr, length, os_translate_purge_advise(advice));
}

void os_strerror(int errnum, char *buf, size_t buflen) {
    #ifdef _GNU_SOURCE
    char *strerror_ret = strerror_r(errnum, buf, buflen);
    if (strerror_ret) {
        (void)strerror_ret;
    }
    #else
    (void)strerror_r(errnum, buf, buflen);
    #endif
}
