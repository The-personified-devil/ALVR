#pragma once

#include <stdint.h>
#include <pthread.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

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

extern AlvrVkExport expt;

extern pthread_mutex_t* queue_mutex;

// TODO: Just fully extract the renderer cuz this will not export because rust is a bitch and I recall why I pushed stuff through rust before giving it right back to c++

// TODO: This will explode lmao
// Should we just give the entire thing to monado
struct CEncoder;

// TODO: Just accept it, rewrite it even more and then integrate it into monado as a full render target
// without these 500 extra weird middle steps
extern struct CEncoder* create_encoder(struct AlvrVkInfo* info);
// TODO: Just pass the create info from monado along here
extern void create_images(struct CEncoder*);
extern void present(struct CEncoder*, uint64_t frame, uint64_t timeline_value, uint32_t img_idx);

extern void alvr_take_vulkan(struct AlvrVkExport*);
