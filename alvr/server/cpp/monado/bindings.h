#pragma once

#include <cstdint>
#include <pthread.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
// #include "../my_header.h"

struct AlvrVkInfo {
	VkInstance instance;
	uint32_t version;
	VkPhysicalDevice physDev;
	uint32_t phyDevIdx;
	VkDevice device;
	uint32_t queueFamIdx;
	uint32_t queueIdx;
	VkQueue queue;
	pthread_mutex_t* queue_mutex;
};

struct AlvrVkImg {
	uint64_t img;
	uint64_t view;
};

struct AlvrVkExport {
    AlvrVkImg imgs[3];
    uint64_t semaphore;
};

extern "C" AlvrVkExport expt;

extern "C" pthread_mutex_t* queue_mutex;
extern "C" pthread_mutex_t render_mutex;
extern "C" pthread_mutex_t double_mutex;

extern "C" uint64_t tl_val;
extern "C" uint32_t img_idx;

extern "C" void entry_point(AlvrVkInfo* info);

extern "C" void signal_enc(uint64_t, uint32_t);
