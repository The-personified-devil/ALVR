#include "bindings.h"
#include "platform/linux/MEncoder.h"
#include <cstdint>
#include <cstdio>
#include <thread>

void signal_enc(uint64_t t, uint32_t i) {
	tl_val = t;
	img_idx = i;
	printf("before mutex lock\n");
	// pthread_mutex_lock(&double_mutex);
	pthread_mutex_unlock(&render_mutex);
	pthread_mutex_lock(&double_mutex);
}

void entry_point(AlvrVkInfo* info) {
	queue_mutex = info->queue_mutex;

	std::thread t1([=]{
	Settings::Instance().Load();
	CEncoder e{};
	e.Rune(info->instance, info->physDev, info->device, info->queueFamIdx);
        e.OnStreamStart();
	});
	t1.detach();
	// t1.join();
}
