#include "kmp.h"
#include "kmp_io.h"
#include "kmp_wrapper_malloc.h"

#include <sicm_low.h>

static void *h_sicm;

static sicm_device_list (*p_sicm_init)(void);
static sicm_arena (*p_sicm_arena_create)(size_t, int, sicm_device_list *);
static void (*p_sicm_arena_destroy)(sicm_arena arena);
static sicm_device *(*p_sicm_arena_get_devices)(sicm_arena sa);
static int (*p_sicm_arena_set_devices)(sicm_arena sa, sicm_device *dev);
static void *(*p_sicm_arena_alloc)(sicm_arena sa, size_t sz);
static void (*p_sicm_free)(void *ptr);
static int (*p_sicm_device_page_size)(sicm_device *);

static int kmp_sicm_init_allocator(kmp_allocator_t *al);
static void *kmp_sicm_alloc(size_t size, kmp_allocator_t *al, int gtid);
static void kmp_sicm_free(void *ptr, kmp_allocator_t *al, int gtid);

static sicm_device_list kmp_sicm_devs;	// all
static sicm_device_list kmp_sicm_default_devs;
static sicm_device_list kmp_sicm_large_cap_devs;
static sicm_device_list kmp_sicm_const_devs;
static sicm_device_list kmp_sicm_high_bw_devs;
static sicm_device_list kmp_sicm_low_lat_devs;

static void kmp_sicm_init_device_list(sicm_device_list *devs, int tag) {
  int n;

  n = 0;
  for(unsigned int i = 0; i < kmp_sicm_devs.count; i++) {
    sicm_device *dev = kmp_sicm_devs.devices[i];
    if (dev->tag == tag && p_sicm_device_page_size(dev) == 4)
      n++;
  }

  devs->count = n;
  devs->devices = (sicm_device **) malloc(n * sizeof(sicm_device *));
  n = 0;
  for(unsigned int i = 0; i < kmp_sicm_devs.count; i++) {
    sicm_device *dev = kmp_sicm_devs.devices[i];
    if (dev->tag == tag && p_sicm_device_page_size(dev) == 4) {
      devs->devices[n] = dev;
      n++;
    }
  }
}

void __kmp_init_sicm() {
#if KMP_OS_UNIX && KMP_DYNAMIC_LIB
	h_sicm = dlopen("libsicm.so", RTLD_LAZY);
	if (!h_sicm) {
//		printf("can't load libsicm.so: %s\n", dlerror());
		KE_TRACE(25, ("can't load libsicm.so: %s\n", dlerror()));
		goto error;
	}

	p_sicm_init = (sicm_device_list (*)(void)) dlsym(h_sicm, "sicm_init");
	p_sicm_arena_create = (sicm_arena (*)(size_t, int, sicm_device_list *)) dlsym(h_sicm, "sicm_arena_create");
	p_sicm_arena_destroy = (void (*)(sicm_arena)) dlsym(h_sicm, "sicm_arena_destroy");
	p_sicm_arena_get_devices = (sicm_device *(*)(sicm_arena sa)) dlsym(h_sicm, "sicm_arena_get_devices");
	p_sicm_arena_set_devices = (int (*)(sicm_arena sa, sicm_device *dev)) dlsym(h_sicm, "sicm_arena_set_devices");
	p_sicm_arena_alloc = (void *(*)(sicm_arena sa, size_t sz)) dlsym(h_sicm, "sicm_arena_alloc");
	p_sicm_free = (void (*)(void *ptr)) dlsym(h_sicm, "sicm_free");
	p_sicm_device_page_size = (int (*)(sicm_device *)) dlsym(h_sicm, "sicm_device_page_size");

	if (!p_sicm_init || !p_sicm_arena_create || !p_sicm_arena_destroy || !p_sicm_arena_get_devices || 
			!p_sicm_arena_set_devices || !p_sicm_arena_alloc || !p_sicm_free || !p_sicm_device_page_size) {
		printf("can't initialize SICM library\n");
		KE_TRACE(25, ("can't initialize SICM library\n"));
		goto error;
	}

	KE_TRACE(25, ("__kmp_init_sicm: Initializing SICM support\n"));
	kmp_sicm_devs = p_sicm_init();
	kmp_init_allocator_p = kmp_sicm_init_allocator;
	kmp_sicm_init_device_list(&kmp_sicm_default_devs, SICM_DRAM);
//	printf("__kmp_init_sicm: Default memspace: %d devices\n", kmp_sicm_default_devs.count);
	KE_TRACE(25, ("__kmp_init_sicm: Default memspace: %d devices\n", kmp_sicm_default_devs.count));
	kmp_sicm_init_device_list(&kmp_sicm_large_cap_devs, SICM_OPTANE);
//	printf("__kmp_init_sicm: Large-capacity memspace: %d devices\n", kmp_sicm_large_cap_devs.count);
	KE_TRACE(25, ("__kmp_init_sicm: Large-capacity memspace: %d devices\n", kmp_sicm_large_cap_devs.count));
	kmp_sicm_init_device_list(&kmp_sicm_const_devs, -1);
//	printf("__kmp_init_sicm: Const memspace: %d devices\n", kmp_sicm_const_devs.count);
	KE_TRACE(25, ("__kmp_init_sicm: Constant memspace: %d devices\n", kmp_sicm_const_devs.count));
	kmp_sicm_init_device_list(&kmp_sicm_high_bw_devs, SICM_KNL_HBM);
//	printf("__kmp_init_sicm: High-bandwidth memspace: %d devices\n", kmp_sicm_high_bw_devs.count);
	KE_TRACE(25, ("__kmp_init_sicm: High-bandwidth memspace: %d devices\n", kmp_sicm_high_bw_devs.count));
	kmp_sicm_init_device_list(&kmp_sicm_low_lat_devs, -1);
//	printf("__kmp_init_sicm: Low-latency memspace: %d devices\n", kmp_sicm_low_lat_devs.count);
	KE_TRACE(25, ("__kmp_init_sicm: Low-latency memspace: %d devices\n", kmp_sicm_low_lat_devs.count));

        for(int i = 0; i < 9; i++)
	  kmp_sicm_init_allocator(&kmp_standard_allocators[i]);

	KE_TRACE(25, ("__kmp_init_sicm: SICM library initialized\n"));
	return;

error:
#endif

	p_sicm_init = NULL;
	p_sicm_arena_create = NULL;
	p_sicm_arena_get_devices = NULL;
	p_sicm_arena_set_devices = NULL;
	p_sicm_arena_alloc = NULL;
	p_sicm_free = NULL;
	if (h_sicm)
		dlclose(h_sicm);
	h_sicm = NULL;

	return;
}

void __kmp_fini_sicm() {
	if (h_sicm)
		dlclose(h_sicm);

	p_sicm_init = NULL;
	p_sicm_arena_create = NULL;
	p_sicm_arena_get_devices = NULL;
	p_sicm_arena_set_devices = NULL;
	p_sicm_arena_alloc = NULL;
	p_sicm_free = NULL;
	h_sicm = NULL;
}

int kmp_sicm_init_allocator(kmp_allocator_t *al) {
  sicm_arena sa;
  sicm_device_list *devs;

  KMP_ASSERT(p_sicm_arena_create != NULL);

  al->aux = NULL;
  devs = NULL;
  if (al->memspace == omp_default_mem_space) {
    devs = &kmp_sicm_default_devs;
  } else if (al->memspace == omp_const_mem_space) {
    devs = &kmp_sicm_const_devs;
  } else if (al->memspace == omp_large_cap_mem_space) {
    devs = &kmp_sicm_large_cap_devs;
  } else if (al->memspace == omp_high_bw_mem_space) {
    devs = &kmp_sicm_high_bw_devs;
  } else if (al->memspace == omp_low_lat_mem_space) {
    devs = &kmp_sicm_low_lat_devs;
  }

//  printf("kmp_sicm_init_allocator: al %p memspace %p devs %d\n", al, al->memspace, devs!=NULL?devs->count:-1);
  if (devs == NULL)
    return -1;

  if (devs->count == 0)
    return -1;

  sa = p_sicm_arena_create(al->pool_size, 0, devs);
  if (sa == NULL) {
//    printf("kmp_sicm_init_allocator: can't create arena\n");
    return -1;
  }

  al->alloc = kmp_sicm_alloc;
  al->free = kmp_sicm_free;

  al->aux = sa;
  return 0;
}

static void *kmp_sicm_alloc(size_t size, kmp_allocator_t *al, int) {
  sicm_arena sa;

  sa = (sicm_arena) al->aux;
  KMP_ASSERT(p_sicm_arena_alloc != NULL);
  return p_sicm_arena_alloc(sa, size);
}

static void kmp_sicm_free(void *ptr, kmp_allocator_t *al, int) {
  KMP_ASSERT(p_sicm_free != NULL);
  p_sicm_free(ptr);
}

void kmp_sicm_destroy_allocator(kmp_allocator_t *al) {
  sicm_arena sa;

  sa = al->aux;
  p_sicm_arena_destroy(sa);
}
