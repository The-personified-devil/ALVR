#include "MEncoder.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
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
#include "alvr_server/PoseHistory.h"
#include "alvr_server/Settings.h"
#include "ffmpeg_helper.h"
#include "protocol.h"

extern "C" {
#include <libavutil/avutil.h>
}

CEncoder::CEncoder(std::shared_ptr<PoseHistory> poseHistory) : m_poseHistory(poseHistory) {}

CEncoder::~CEncoder() { Stop(); }

pthread_mutex_t render_mutex;
pthread_mutex_t double_mutex;

uint64_t tl_val;
uint32_t img_idx;

void* g_poseHistory = nullptr;
void* g_encoder;

namespace {
void read_exactly(pollfd pollfds, char *out, size_t size, std::atomic_bool &exiting) {
    while (not exiting and size != 0) {
        int timeout = 1; // poll api doesn't fit perfectly(100 mircoseconds) poll uses milliseconds
                         // we do the best we can(1000 mircoseconds)
        pollfds.events = POLLIN;
        int count = poll(&pollfds, 1, timeout);
        if (count < 0) {
            throw MakeException("poll failed: %s", strerror(errno));
        } else if (count == 1) {
            int s = read(pollfds.fd, out, size);
            if (s == -1) {
                throw MakeException("read failed: %s", strerror(errno));
            }
            out += s;
            size -= s;
        }
    }
}

void read_latest(pollfd pollfds, char *out, size_t size, std::atomic_bool &exiting) {
    read_exactly(pollfds, out, size, exiting);
    while (not exiting) {
        int timeout = 0; // poll api fixes the original perfectly(0 microseconds)
        pollfds.events = POLLIN;
        int count = poll(&pollfds, 1, timeout);
        if (count == 0)
            return;
        read_exactly(pollfds, out, size, exiting);
    }
}

int accept_timeout(pollfd socket, std::atomic_bool &exiting) {
    while (not exiting) {
        int timeout = 15; // poll api also fits the original perfectly(15000 microseconds)
        socket.events = POLLIN;
        int count = poll(&socket, 1, timeout);
        if (count < 0) {
            throw MakeException("poll failed: %s", strerror(errno));
        } else if (count == 1) {
            return accept4(socket.fd, NULL, NULL, SOCK_CLOEXEC);
        }
    }
    return -1;
}

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

// void CEncoder::GetFds(int client, int (*received_fds)[6]) {
//     struct msghdr msg;
//     struct cmsghdr *cmsg;
//     union {
//         struct cmsghdr cm;
//         u_int8_t pktinfo_sizer[sizeof(struct cmsghdr) + 1024];
//     } control_un;
//     struct iovec iov[1];
//     char data[1];
//     int ret;

//     msg.msg_control = &control_un;
//     msg.msg_controllen = sizeof(control_un);
//     msg.msg_flags = 0;
//     msg.msg_name = NULL;
//     msg.msg_namelen = 0;
//     iov[0].iov_base = data;
//     iov[0].iov_len = 1;
//     msg.msg_iov = iov;
//     msg.msg_iovlen = 1;

//     ret = recvmsg(client, &msg, 0);
//     if (ret == -1) {
//       throw MakeException("recvmsg failed: %s", strerror(errno));
//     }

//     for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
//         if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
//             memcpy(received_fds, CMSG_DATA(cmsg), sizeof(*received_fds));
//             break;
//         }
//     }

//     if (cmsg == NULL) {
//       throw MakeException("cmsg is NULL");
//     }
// }

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
    init_packet init{};
	init.num_images = 3;

    init.image_create_info = {};
    init.image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    init.image_create_info.imageType = VK_IMAGE_TYPE_2D;
    init.image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    init.image_create_info.extent.width = 2000;
    init.image_create_info.extent.height = 2000;
    init.image_create_info.extent.depth = 1;
    init.image_create_info.mipLevels = 1;
    init.image_create_info.arrayLayers = 1;
    init.image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    init.image_create_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    init.image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    init.image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // check that pointer types are null, other values would not make sense over a socket
    assert(init.image_create_info.queueFamilyIndexCount == 0);
    assert(init.image_create_info.pNext == NULL);

    try {
        m_connected = true;

        fprintf(stderr, "\n\nWe are initalizing Vulkan in CEncoder thread\n\n\n");

        av_log_set_callback(av_logfn);

        alvr::VkContext vk_ctx(instance, physDev, dev, queueFamilyIndex);

        FrameRender render(vk_ctx, init);
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

        present_packet frame_info{};

        DriverReadyIdle(true);
		m_poseHistory = std::make_shared<PoseHistory>();

		g_poseHistory = (void*)m_poseHistory.get();
		g_encoder = (void*)this;

	
        while (not m_exiting) {
	    pthread_mutex_lock(&render_mutex);

	    // TODO: Ginourmously unfuck this
            frame_info.semaphore_value = tl_val;
	    frame_info.image = img_idx;

	    // TODO: What does this actually do
            frame_info.frame++;

		std::cout << "encode_pipeline-: " << &encode_pipeline << std::endl;
            encode_pipeline->SetParams(GetDynamicEncoderParams());

	    std::cout << "pose history: " << m_poseHistory << std::endl;
            auto pose = m_poseHistory->GetBestPoseMatch((const vr::HmdMatrix34_t &)frame_info.pose);
            if (!pose) {
				std::cout << "skipped :(\n";
	    	pthread_mutex_unlock(&double_mutex);
                continue;
            }

            // m_captureFrame = true;
            if (m_captureFrame) {
                m_captureFrame = false;
                render.CaptureInputFrame(Settings::Instance().m_captureFrameDir +
                                         "/alvr_frame_input.ppm");
                render.CaptureOutputFrame(Settings::Instance().m_captureFrameDir +
                                          "/alvr_frame_output.ppm");
            }

            render.Render(frame_info.image, frame_info.semaphore_value);

            if (!valid_timestamps) {
                ReportPresent(pose->targetTimestampNs, 0);
                ReportComposed(pose->targetTimestampNs, 0);
            }

            bool is_idr = m_scheduler.CheckIDRInsertion();
            encode_pipeline->PushFrame(pose->targetTimestampNs, is_idr);
			if (is_idr) {
				std::cout << "check idr insertion true\n";
			}

            static_assert(sizeof(frame_info.pose) == sizeof(vr::HmdMatrix34_t &));

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

                ReportPresent(pose->targetTimestampNs, present_offset);
                ReportComposed(pose->targetTimestampNs, composed_offset);
		if (packet.isIDR) {
			std::cout << "packet is idr\n" << packet.isIDR;
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
    // Info("CEncoder::Run\n");
    // m_socketPath = getenv("XDG_RUNTIME_DIR");
    // m_socketPath += "/alvr-ipc";

    // int ret;
    // // we don't really care about what happends with unlink, it's just incase we crashed before
    // this
    // // run
    // ret = unlink(m_socketPath.c_str());

    // m_socket.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    // struct sockaddr_un name;
    // if (m_socket.fd == -1) {
    //     perror("socket");
    //     exit(1);
    // }

    // memset(&name, 0, sizeof(name));
    // name.sun_family = AF_UNIX;
    // strncpy(name.sun_path, m_socketPath.c_str(), sizeof(name.sun_path) - 1);

    // ret = bind(m_socket.fd, (const struct sockaddr *)&name, sizeof(name));
    // if (ret == -1) {
    //     perror("bind");
    //     exit(1);
    // }

    // ret = listen(m_socket.fd, 1024);
    // if (ret == -1) {
    //     perror("listen");
    //     exit(1);
    // }

    // Info("CEncoder Listening\n");
    // struct pollfd client;
    // client.fd = accept_timeout(m_socket, m_exiting);
    // if (m_exiting)
    //     return;
    // init_packet init;
    // client.events = POLLIN;
    // read_exactly(client, (char *)&init, sizeof(init), m_exiting);
    // if (m_exiting)
    //     return;

    // // check that pointer types are null, other values would not make sense over a socket
    // assert(init.image_create_info.queueFamilyIndexCount == 0);
    // assert(init.image_create_info.pNext == NULL);

    // char ifbuf[256];
    // char ifbuf2[256];
    // sprintf(ifbuf, "/proc/%d/cmdline", (int)init.source_pid);
    // std::ifstream ifscmdl(ifbuf);
    // ifscmdl >> ifbuf2;
    // Info("CEncoder client connected, pid %d, cmdline %s\n", (int)init.source_pid, ifbuf2);

    // try {
    //     // GetFds(client.fd, &m_fds);

    //     m_connected = true;

    //     fprintf(stderr, "\n\nWe are initalizing Vulkan in CEncoder thread\n\n\n");

    //     av_log_set_callback(av_logfn);

    //     alvr::VkContext vk_ctx(init.device_uuid.data(), {});

    //     FrameRender render(vk_ctx, init, );
    //     auto output = render.CreateOutput();

    //     alvr::VkFrameCtx vk_frame_ctx(vk_ctx, output.imageInfo);
    //     alvr::VkFrame frame(
    //         vk_ctx, output.image, output.imageInfo, output.size, output.memory, output.drm);
    //     auto encode_pipeline = alvr::EncodePipeline::Create(&render,
    //                                                         vk_ctx,
    //                                                         frame,
    //                                                         vk_frame_ctx,
    //                                                         render.GetEncodingWidth(),
    //                                                         render.GetEncodingHeight());

    //     bool valid_timestamps = true;

    //     fprintf(stderr, "CEncoder starting to read present packets");
    //     present_packet frame_info;
    //     while (not m_exiting) {
    //         read_latest(client, (char *)&frame_info, sizeof(frame_info), m_exiting);

    //         encode_pipeline->SetParams(GetDynamicEncoderParams());

    //         auto pose = m_poseHistory->GetBestPoseMatch((const vr::HmdMatrix34_t
    //         &)frame_info.pose); if (!pose) {
    //             continue;
    //         }

    //         if (m_captureFrame) {
    //             m_captureFrame = false;
    //             render.CaptureInputFrame(Settings::Instance().m_captureFrameDir +
    //                                      "/alvr_frame_input.ppm");
    //             render.CaptureOutputFrame(Settings::Instance().m_captureFrameDir +
    //                                       "/alvr_frame_output.ppm");
    //         }

    //         render.Render(frame_info.image, frame_info.semaphore_value);

    //         if (!valid_timestamps) {
    //             ReportPresent(pose->targetTimestampNs, 0);
    //             ReportComposed(pose->targetTimestampNs, 0);
    //         }

    //         encode_pipeline->PushFrame(pose->targetTimestampNs, m_scheduler.CheckIDRInsertion());

    //         static_assert(sizeof(frame_info.pose) == sizeof(vr::HmdMatrix34_t &));

    //         alvr::FramePacket packet;
    //         if (!encode_pipeline->GetEncoded(packet)) {
    //             Error("Failed to get encoded data!");
    //             continue;
    //         }

    //         if (valid_timestamps) {
    //             auto render_timestamps = render.GetTimestamps();
    //             auto encode_timestamp = encode_pipeline->GetTimestamp();

    //             uint64_t present_offset = render_timestamps.now - render_timestamps.renderBegin;
    //             uint64_t composed_offset = 0;

    //             valid_timestamps = render_timestamps.now != 0;

    //             if (encode_timestamp.gpu) {
    //                 composed_offset = render_timestamps.now - encode_timestamp.gpu;
    //             } else if (encode_timestamp.cpu) {
    //                 auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
    //                                std::chrono::steady_clock::now().time_since_epoch())
    //                                .count();
    //                 composed_offset = now - encode_timestamp.cpu;
    //             } else {
    //                 composed_offset = render_timestamps.now - render_timestamps.renderComplete;
    //             }

    //             if (present_offset < composed_offset) {
    //                 present_offset = composed_offset;
    //             }

    //             ReportPresent(pose->targetTimestampNs, present_offset);
    //             ReportComposed(pose->targetTimestampNs, composed_offset);
    //         }

    //         ParseFrameNals(
    //             encode_pipeline->GetCodec(), packet.data, packet.size, packet.pts, packet.isIDR);
    //     }
    // } catch (std::exception &e) {
    //     std::stringstream err;
    //     err << "error in encoder thread: " << e.what();
    //     Error(err.str().c_str());
    // }

    // client.events = POLLHUP;
    // close(client.fd);
}

void CEncoder::Stop() {
    m_exiting = true;
    m_socket.events = POLLHUP;
    close(m_socket.fd);
    unlink(m_socketPath.c_str());
}

void CEncoder::OnStreamStart() { m_scheduler.OnStreamStart(); }

void CEncoder::OnPacketLoss() { m_scheduler.OnPacketLoss(); }

void CEncoder::InsertIDR() { m_scheduler.InsertIDR(); }

void CEncoder::CaptureFrame() { m_captureFrame = true; }
