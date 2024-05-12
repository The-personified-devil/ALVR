#include "MEncoder.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <poll.h>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <stdlib.h>
#include <string>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>

#include "ALVR-common/packet_types.h"
#include "EncodePipeline.h"
#include "FrameRender.h"
#include "alvr_server/Logger.h"
#include "alvr_server/Settings.h"
#include "ffmpeg_helper.h"

extern "C" {
#include <libavutil/avutil.h>
}

CEncoder::CEncoder() {}

CEncoder::~CEncoder() { Stop(); }

pthread_mutex_t render_mutex;
pthread_mutex_t double_mutex;

uint64_t tl_val;
uint32_t img_idx;

void *g_encoder;

namespace {

void av_logfn(void *, int level, const char *data, va_list va) {
    if (level >
#ifdef DEBUG
        AV_LOG_DEBUG)
#else
        AV_LOG_INFO)
#endif
        return;

    char buf[256];
    vsnprintf(buf, sizeof(buf), data, va);

    if (level <= AV_LOG_ERROR)
        Error("Encoder: %s", buf);
    else
        Info("Encoder: %s", buf);
}

} // namespace

void CEncoder::Rune(VkInstance instance,
                    VkPhysicalDevice physDev,
                    VkDevice dev,
                    uint32_t queueFamilyIndex) {
    Info("CEncoder::Run\n");

    tl_val = 0;
    img_idx = 0;

    pthread_mutex_init(&render_mutex, NULL);
    pthread_mutex_init(&double_mutex, NULL);
    pthread_mutex_lock(&render_mutex);

    sleep(2);

    Info("CEncoder Listening\n");
    uint32_t num_images = 3;

    VkImageCreateInfo image_create_info{};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_create_info.extent.width = 2000;
    image_create_info.extent.height = 2000;
    image_create_info.extent.depth = 1;
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // check that pointer types are null, other values would not make sense over a socket
    assert(image_create_info.queueFamilyIndexCount == 0);
    assert(image_create_info.pNext == NULL);

    try {
        fprintf(stderr, "\n\nWe are initalizing Vulkan in CEncoder thread\n\n\n");

        av_log_set_callback(av_logfn);

        alvr::VkContext vk_ctx(instance, physDev, dev, queueFamilyIndex);

        FrameRender render(vk_ctx, num_images, image_create_info);
        auto output = render.CreateOutput();

        alvr::VkFrameCtx vk_frame_ctx(vk_ctx, output.imageInfo);
        alvr::VkFrame frame(
            vk_ctx, output.image, output.imageInfo, output.size, output.memory, output.drm);
        auto encode_pipeline = alvr::EncodePipeline::Create(&render,
                                                            vk_ctx,
                                                            frame,
                                                            vk_frame_ctx,
                                                            render.GetEncodingWidth(),
                                                            render.GetEncodingHeight());

        bool valid_timestamps = true;

        fprintf(stderr, "CEncoder starting to read present packets");

        DriverReadyIdle(true);
        g_encoder = (void *)this;

        while (not m_exiting) {
            pthread_mutex_lock(&render_mutex);

            // TODO: Ginourmously unfuck this
            frame_info.semaphore_value = tl_val;
            frame_info.image = img_idx;

            // TODO: What does this actually do
            frame_info.frame++;

            encode_pipeline->SetParams(GetDynamicEncoderParams());

            if (m_captureFrame) {
                m_captureFrame = false;
                render.CaptureInputFrame(Settings::Instance().m_captureFrameDir +
                                         "/alvr_frame_input.ppm");
                render.CaptureOutputFrame(Settings::Instance().m_captureFrameDir +
                                          "/alvr_frame_output.ppm");
            }

            render.Render(frame_info.image, frame_info.semaphore_value);

            bool is_idr = m_scheduler.CheckIDRInsertion();
            encode_pipeline->PushFrame(0, is_idr);

            alvr::FramePacket packet;
            if (!encode_pipeline->GetEncoded(packet)) {
                Error("Failed to get encoded data!");
                continue;
            }

            if (valid_timestamps) {
                auto render_timestamps = render.GetTimestamps();
                auto encode_timestamp = encode_pipeline->GetTimestamp();

                uint64_t present_offset = render_timestamps.now - render_timestamps.renderBegin;
                uint64_t composed_offset = 0;

                valid_timestamps = render_timestamps.now != 0;

                if (encode_timestamp.gpu) {
                    composed_offset = render_timestamps.now - encode_timestamp.gpu;
                } else if (encode_timestamp.cpu) {
                    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count();
                    composed_offset = now - encode_timestamp.cpu;
                } else {
                    composed_offset = render_timestamps.now - render_timestamps.renderComplete;
                }

                if (present_offset < composed_offset) {
                    present_offset = composed_offset;
                }
            }

            ParseFrameNals(
                encode_pipeline->GetCodec(), packet.data, packet.size, packet.pts, packet.isIDR);

            pthread_mutex_unlock(&double_mutex);
        }
    } catch (std::exception &e) {
        std::stringstream err;
        err << "error in encoder thread: " << e.what();
        Error(err.str().c_str());
    }
}

void CEncoder::Run() {
    // TODO: Use this or spawn thread manually?
}

void CEncoder::Stop() { m_exiting = true; }

void CEncoder::OnStreamStart() { m_scheduler.OnStreamStart(); }

void CEncoder::OnPacketLoss() { m_scheduler.OnPacketLoss(); }

void CEncoder::InsertIDR() { m_scheduler.InsertIDR(); }

void CEncoder::CaptureFrame() { m_captureFrame = true; }
