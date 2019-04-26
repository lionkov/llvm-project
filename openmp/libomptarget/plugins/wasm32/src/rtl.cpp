//===----RTLs/wasm32/src/rtl.cpp - Target RTLs Implementation ------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// RTL for WeebAssemblly machine
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstddef>
#include <list>
#include <string>
#include <vector>
#include <map>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "omptargetplugin.h"

#ifndef TARGET_NAME
#define TARGET_NAME WASM32
#endif

#ifdef OMPTARGET_DEBUG
static int DebugLevel = 1;

#define GETNAME2(name) #name
#define GETNAME(name) GETNAME2(name)
#define DP(...) \
  do { \
    if (DebugLevel > 0) { \
      DEBUGP("Target " GETNAME(TARGET_NAME) " RTL", __VA_ARGS__); \
    } \
  } while (false)
#else // OMPTARGET_DEBUG
#define DP(...) {}
#endif // OMPTARGET_DEBUG

#include "../../common/elf_common.c"

/// Keep entries table per device.
struct FuncOrGblEntryTy {
  __tgt_target_table Table;
  std::vector<__tgt_offload_entry> Entries;
};

/// Device envrionment data
/// Manually sync with the deviceRTL side for now, move to a dedicated header file later.
struct omptarget_device_environmentTy {
  int32_t debug_level;
};

struct DataTy {
	void*	hostptr;
	void*	devptr;
	int64_t	size;		// can't really be more than 4GB
	void*	buf;		// can be NULL if uninitialized
};

struct DeviceTy {
	std::vector<__tgt_offload_entry>	Entries;
	__tgt_target_table			Table;

	int64_t					DataPtrNext;
	std::map<int64_t, DataTy*>		Data;

	// we don't really need this
	// OpenMP properties
//	int				NumTeams;
//	int				NumThreads;

	void addOffloadEntry( __tgt_offload_entry& entry) {
		Entries.push_back(entry);
	}

	__tgt_offload_entry* findOffloadEntry(void *addr) {
		for (auto &it : Entries) {
			if (it.addr == addr)
				return &it;
		}

		return NULL;
	}

	// Return the pointer to the target entries table
	__tgt_target_table *getOffloadEntriesTable() {
		int32_t size = Entries.size();

		// Table is empty
		if (!size)
			return 0;

		__tgt_offload_entry *begin = &Entries[0];
		__tgt_offload_entry *end = &Entries[size - 1];

		// Update table info according to the entries and return the pointer
		Table.EntriesBegin = begin;
		Table.EntriesEnd = ++end;

		return &Table;
	}

	void clearOffloadEntriesTable() {
		Entries.clear();
		Table.EntriesBegin = Table.EntriesEnd = 0;
	}

	void *allocData(void *hostptr, int64_t size) {
		DataTy *al;
		int64_t devptr;

		if (size == 0 || size >= 0x100000000L)
			return NULL;
 
		devptr = DataPtrNext;
		DataPtrNext += size;

		al = new DataTy;
		al->hostptr = hostptr;
		al->devptr = (void *) devptr;
		al->size = size;
		al->buf = NULL;

		Data.insert(std::make_pair(devptr, al));
		return al->devptr;
	}

	bool copyToData(void *devptr, void *hostptr, int64_t size) {
		DataTy *al = Data[(int64_t) devptr];

		if (al == NULL)
			return false;

		if (al->size != size)
			return false;

		if (al->buf == NULL) {
			al->buf = malloc(al->size);
			if (al->buf == NULL)
				return false;
		}

		memcpy(al->buf, hostptr, size);
		return true;
	}

	bool copyFromData(void *hostptr, void *devptr, int64_t size) {
		DataTy *al = Data[(int64_t) devptr];

		if (al == NULL || al->buf == NULL)
			return false;

		if (al->size != size)
			return false;

		memcpy(hostptr, al->buf, size);
		return true;
	}

	bool deleteData(void *devptr) {
		DataTy *al = Data[(int64_t) devptr];

		if (al == NULL)
			return false;

		if (al->buf) {
			free(al->buf);
			al->buf = NULL;
		}

		Data.erase((int64_t) devptr);
		delete(al);

		return true;
	}
};

/// Class containing all the device information.
class RTLDeviceInfoTy {
	std::vector<DeviceTy> Devices;

public:
	int NumberOfDevices;

	// OpenMP Environment properties
	int EnvNumTeams;
	int EnvTeamLimit;

	//static int EnvNumThreads;
	static const int HardTeamLimit = 1<<16; // 64k
	static const int HardThreadLimit = 1024;
	static const int DefaultNumTeams = 128;
	static const int DefaultNumThreads = 128;

	RTLDeviceInfoTy() {
#ifdef OMPTARGET_DEBUG
		if (char *envStr = getenv("LIBOMPTARGET_DEBUG")) {
			DebugLevel = std::stoi(envStr);
		}
#endif // OMPTARGET_DEBUG

		// TODO: put the actual ceph code once it is implemented
		NumberOfDevices = 1;

		Devices.resize(NumberOfDevices);

		// Get environment variables regarding teams
		char *envStr = getenv("OMP_TEAM_LIMIT");
		if (envStr) {
			// OMP_TEAM_LIMIT has been set
			EnvTeamLimit = std::stoi(envStr);
			DP("Parsed OMP_TEAM_LIMIT=%d\n", EnvTeamLimit);
		} else {
			EnvTeamLimit = -1;
		}

		envStr = getenv("OMP_NUM_TEAMS");
		if (envStr) {
			// OMP_NUM_TEAMS has been set
			EnvNumTeams = std::stoi(envStr);
			DP("Parsed OMP_NUM_TEAMS=%d\n", EnvNumTeams);
		} else {
			EnvNumTeams = -1;
		}
	}

	~RTLDeviceInfoTy() {
		// TODO: free the ceph resources
	}

	DeviceTy& getDevice(int32_t device_id) {
		assert(device_id < (int32_t)Devices.size() && "Unexpected device id!");
		return Devices[device_id];
	}

};

static RTLDeviceInfoTy DeviceInfo;

#ifdef __cplusplus
extern "C" {
#endif

int32_t __tgt_rtl_is_valid_binary(__tgt_device_image *image) {
//  return elf_check_machine(image, 190); // EM_CUDA = 190.
    char *start = (char *) image->ImageStart;
    char *end = (char *) image->ImageEnd;
    int size = end - start;

    if (size < 4)
	return false;

    // wasm module starts with \0asm
    if (start[0] != '\0' || start[1] != 'a' || start[2] != 's' || start[3] != 'm')
	return false;

    return true;
}

int32_t __tgt_rtl_number_of_devices() {
	return DeviceInfo.NumberOfDevices;
}

int32_t __tgt_rtl_init_device(int32_t device_id) {
	DeviceTy& dev = DeviceInfo.getDevice(device_id);

	dev.DataPtrNext = 16;	// TODO: is this good number?
	
	// TODO: initialize ceph?
	return OFFLOAD_SUCCESS;
}

__tgt_target_table *__tgt_rtl_load_binary(int32_t device_id, __tgt_device_image *image) {
	DeviceTy& dev = DeviceInfo.getDevice(device_id);

	// Clear the offload table as we are going to create a new one.
	dev.clearOffloadEntriesTable();

	// Create the module and extract the function pointers.
	DP("Load data from image " DPxMOD " " DPxMOD "\n", DPxPTR(image->ImageStart), DPxPTR(image->ImageEnd));
	// TODO: 

	DP("Wasm32 module successfully loaded!\n");

	// Find the symbols in the module by name.
	__tgt_offload_entry *HostBegin = image->EntriesBegin;
	__tgt_offload_entry *HostEnd = image->EntriesEnd;

	for (__tgt_offload_entry *e = HostBegin; e != HostEnd; ++e) {

		if (!e->addr) {
			// We return NULL when something like this happens, the host should have
			// always something in the address to uniquely identify the target region.
			DP("Invalid binary: host entry '<null>' (size = %zd)...\n", e->size);

			return NULL;
		}

		if (e->size) {
			// TODO: no idea what this is
			DP("Entry point to global %s\n", e->name);
			dev.addOffloadEntry(*e);
		} else {
			DP("Entry point %s\n", e->name);

//			__tgt_offload_entry entry = *e;
			dev.addOffloadEntry(*e);
		}
	}

	{
		int fd = open("omp.wasm", O_CREAT | O_RDWR | O_TRUNC, 0666);
		write(fd, image->ImageStart, (char *) image->ImageEnd - (char *) image->ImageStart);
		close(fd);
  	}

	return dev.getOffloadEntriesTable();
}

void *__tgt_rtl_data_alloc(int32_t device_id, int64_t size, void *hst_ptr) {
	DeviceTy& dev = DeviceInfo.getDevice(device_id);

	DP("Data alloc size %ld hostptr %p\n", size, hst_ptr);
	return dev.allocData(hst_ptr, size);
}

int32_t __tgt_rtl_data_submit(int32_t device_id, void *tgt_ptr, void *hst_ptr, int64_t size) {
	DeviceTy& dev = DeviceInfo.getDevice(device_id);

	DP("Data submit size %ld tgtptr %p hostptr %p\n", size, tgt_ptr, hst_ptr);
	return dev.copyToData(tgt_ptr, hst_ptr, size)?OFFLOAD_SUCCESS:OFFLOAD_FAIL;
}

int32_t __tgt_rtl_data_retrieve(int32_t device_id, void *hst_ptr, void *tgt_ptr, int64_t size) {
	DeviceTy& dev = DeviceInfo.getDevice(device_id);

	DP("Data retrieve size %ld tgtptr %p hostptr %p\n", size, tgt_ptr, hst_ptr);
	return dev.copyFromData(hst_ptr, tgt_ptr, size)?OFFLOAD_SUCCESS:OFFLOAD_FAIL;
}

int32_t __tgt_rtl_data_delete(int32_t device_id, void *tgt_ptr) {
	DeviceTy& dev = DeviceInfo.getDevice(device_id);

	DP("Data delete tgtptr %p\n", tgt_ptr);
	return dev.deleteData(tgt_ptr)?OFFLOAD_SUCCESS:OFFLOAD_FAIL;
}

int32_t __tgt_rtl_run_target_team_region(int32_t device_id, void *tgt_entry_ptr,
    void **tgt_args, ptrdiff_t *tgt_offsets, int32_t arg_num, int32_t team_num,
    int32_t thread_limit, uint64_t loop_tripcount) {

	return __tgt_rtl_run_target_region(device_id, tgt_entry_ptr, tgt_args, tgt_offsets, arg_num);
}

int32_t __tgt_rtl_run_target_region(int32_t device_id, void *tgt_entry_ptr,
    void **tgt_args, ptrdiff_t *tgt_offsets, int32_t arg_num) {

	DeviceTy& dev = DeviceInfo.getDevice(device_id);
	__tgt_offload_entry* e = dev.findOffloadEntry(tgt_entry_ptr);

	if (e == NULL) {
		DP("__tgt_rtl_run_target_region: can't find entry point %p\n", tgt_entry_ptr);
		return OFFLOAD_FAIL;
	}

	DP("target team region: entry '%s' arg_num %d\n", e->name, arg_num);
	for(int i = 0; i < arg_num; i++) {
		DP("\tArg %d: %p + %ld\n", i, tgt_args[i], tgt_offsets[i]);
	}

	// TODO
	return OFFLOAD_SUCCESS;
}

#ifdef __cplusplus
}
#endif
