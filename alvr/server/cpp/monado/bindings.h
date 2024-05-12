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

// TODO: This will explode lmao
// Should we just give the entire thing to monado
struct CEncoder;

// TODO: Just accept it, rewrite it even more and then integrate it into monado as a full render target
// without these 500 extra weird middle steps
extern "C" CEncoder* create_encoder(AlvrVkInfo* info);
extern "C" void create_images(CEncoder);
extern "C" void present(CEncoder, uint64_t frame, uint64_t timeline_value, uint32_t img_idx);
