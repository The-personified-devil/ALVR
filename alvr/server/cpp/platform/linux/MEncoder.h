#pragma once

#include "alvr_server/IDRScheduler.h"
#include "shared/threadtools.h"
#include <atomic>
#include <memory>
#include <vector>
#include <poll.h>
#include <sys/types.h>
#include <vulkan/vulkan_core.h>

class PoseHistory;

class CEncoder : public CThread {
  public:
    CEncoder();
    ~CEncoder();
    bool Init() override { return true; }
    void Run() override;
    void Rune(VkInstance, VkPhysicalDevice, VkDevice, uint32_t);

    void Stop();
    void OnStreamStart();
    void OnPacketLoss();
    void InsertIDR();
    bool IsConnected() {
		// TODO: Replace or delete
		return true;
	}
    void CaptureFrame();

  private:
    std::atomic_bool m_exiting{false};
    IDRScheduler m_scheduler;
    std::atomic_bool m_captureFrame = false;
};
