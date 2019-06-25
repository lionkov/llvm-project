/*
 * kmp_memkind.cpp -- support for memkind memory allocations
 */

//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


#include "kmp.h"
#include "kmp_io.h"
#include "kmp_wrapper_malloc.h"

static const char *kmp_mk_lib_name;
static void *h_memkind;
/* memkind experimental API: */
// memkind_alloc
static void *(*kmp_mk_alloc)(void *k, size_t sz);
// memkind_free
static void (*kmp_mk_free)(void *kind, void *ptr);
// memkind_check_available
static int (*kmp_mk_check)(void *kind);
// kinds we are going to use
static void **mk_default;
static void **mk_interleave;
static void **mk_hbw;
static void **mk_hbw_interleave;
static void **mk_hbw_preferred;
static void **mk_hugetlb;
static void **mk_hbw_hugetlb;
static void **mk_hbw_preferred_hugetlb;

static void *kmp_memkind_alloc(size_t size, kmp_allocator_t *al, int gtid);
static void kmp_memkind_free(void *ptr, kmp_allocator_t *al, int gtid);

#if KMP_OS_UNIX && KMP_DYNAMIC_LIB
static inline void chk_kind(void ***pkind) {
  KMP_DEBUG_ASSERT(pkind);
  if (*pkind) // symbol found
    if (kmp_mk_check(**pkind)) // kind not available or error
      *pkind = NULL;
}
#endif

void __kmp_init_memkind() {
// as of 2018-07-31 memkind does not support Windows*, exclude it for now
#if KMP_OS_UNIX && KMP_DYNAMIC_LIB
  // use of statically linked memkind is problematic, as it depends on libnuma
  kmp_mk_lib_name = "libmemkind.so";
  h_memkind = dlopen(kmp_mk_lib_name, RTLD_LAZY);
  if (!h_memkind)
    return;

  kmp_mk_check = (int (*)(void *))dlsym(h_memkind, "memkind_check_available");
  kmp_mk_alloc =
        (void *(*)(void *, size_t))dlsym(h_memkind, "memkind_malloc");
  kmp_mk_free = (void (*)(void *, void *))dlsym(h_memkind, "memkind_free");
  mk_default = (void **)dlsym(h_memkind, "MEMKIND_DEFAULT");
  if (kmp_mk_check && kmp_mk_alloc && kmp_mk_free && mk_default &&
      !kmp_mk_check(*mk_default)) {
    __kmp_memkind_available = 1;
    mk_interleave = (void **)dlsym(h_memkind, "MEMKIND_INTERLEAVE");
    chk_kind(&mk_interleave);
    mk_hbw = (void **)dlsym(h_memkind, "MEMKIND_HBW");
    chk_kind(&mk_hbw);
    mk_hbw_interleave = (void **)dlsym(h_memkind, "MEMKIND_HBW_INTERLEAVE");
    chk_kind(&mk_hbw_interleave);
    mk_hbw_preferred = (void **)dlsym(h_memkind, "MEMKIND_HBW_PREFERRED");
    chk_kind(&mk_hbw_preferred);
    mk_hugetlb = (void **)dlsym(h_memkind, "MEMKIND_HUGETLB");
    chk_kind(&mk_hugetlb);
    mk_hbw_hugetlb = (void **)dlsym(h_memkind, "MEMKIND_HBW_HUGETLB");
    chk_kind(&mk_hbw_hugetlb);
    mk_hbw_preferred_hugetlb =
        (void **)dlsym(h_memkind, "MEMKIND_HBW_PREFERRED_HUGETLB");
    chk_kind(&mk_hbw_preferred_hugetlb);
    KE_TRACE(25, ("__kmp_init_memkind: memkind library initialized\n"));

    for(int i = 0; i < 9; i++) {
      kmp_standard_allocators[0].alloc = kmp_memkind_alloc;
      kmp_standard_allocators[1].free = kmp_memkind_free;
    }
    return; // success
  }
  dlclose(h_memkind); // failure
  h_memkind = NULL;
}
#endif


void __kmp_fini_memkind() {
#if KMP_OS_UNIX && KMP_DYNAMIC_LIB
  if (__kmp_memkind_available)
    KE_TRACE(25, ("__kmp_fini_memkind: finalize memkind library\n"));
  if (h_memkind) {
    dlclose(h_memkind);
    h_memkind = NULL;
  }
  kmp_mk_check = NULL;
  kmp_mk_alloc = NULL;
  kmp_mk_free = NULL;
  mk_default = NULL;
  mk_interleave = NULL;
  mk_hbw = NULL;
  mk_hbw_interleave = NULL;
  mk_hbw_preferred = NULL;
  mk_hugetlb = NULL;
  mk_hbw_hugetlb = NULL;
  mk_hbw_preferred_hugetlb = NULL;
#endif
}

static void *kmp_memkind_alloc(size_t size, kmp_allocator_t *al, int) {
  if (al->partition == OMP_ATV_INTERLEAVED && mk_interleave)
    return kmp_mk_alloc(*mk_interleave, size);

  if (al->memspace == omp_high_bw_mem_space)
    return kmp_mk_alloc(*mk_hbw_preferred, size);

  return kmp_mk_alloc(*mk_default, size);
}

static void kmp_memkind_free(void *ptr, kmp_allocator_t *al, int) {
  if (al->partition == OMP_ATV_INTERLEAVED && mk_interleave)
    return kmp_mk_free(*mk_interleave, ptr);

  if (al->memspace == omp_high_bw_mem_space)
    return kmp_mk_free(*mk_hbw_preferred, ptr);

  return kmp_mk_free(*mk_default, ptr);
  
}
