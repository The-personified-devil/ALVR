/* ALVR is licensed under the MIT license. https://github.com/alvr-org/ALVR/blob/master/LICENSE */

#pragma once

/* Warning, this file is autogenerated by cbindgen. Don't modify this manually. */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define WS_BROADCAST_CAPACITY 256

typedef struct AlvrTargetConfig {
    uint32_t target_width;
    uint32_t target_height;
} AlvrTargetConfig;

typedef struct AlvrBatteryValue {
    uint64_t device_id;
    // range [0, 1]
    float value;
} AlvrBatteryValue;

typedef enum AlvrEvent_Tag {
    ALVR_EVENT_BATTERY,
    ALVR_EVENT_BOUNDS,
    ALVR_EVENT_RESTART,
    ALVR_EVENT_SHUTDOWN,
} AlvrEvent_Tag;

typedef struct AlvrEvent {
    AlvrEvent_Tag tag;
    union {
        struct {
            struct AlvrBatteryValue battery;
        };
        struct {
            float bounds[2];
        };
    };
} AlvrEvent;

typedef struct AlvrDeviceConfig {
    uint64_t device_id;
    uint64_t interaction_profile_id;
} AlvrDeviceConfig;

typedef union AlvrInputValue {
    bool bool_;
    float float_;
} AlvrInputValue;

typedef struct AlvrInput {
    uint64_t id;
    union AlvrInputValue value;
} AlvrInput;

typedef struct AlvrQuat {
    float x;
    float y;
    float z;
    float w;
} AlvrQuat;

typedef struct AlvrPose {
    struct AlvrQuat orientation;
    float position[3];
} AlvrPose;

typedef struct AlvrSpaceRelation {
    struct AlvrPose pose;
    float linear_velocity[3];
    float angular_velocity[3];
    bool has_velocity;
} AlvrSpaceRelation;

typedef struct AlvrJoint {
    struct AlvrSpaceRelation relation;
    float radius;
} AlvrJoint;

typedef struct AlvrJointSet {
    struct AlvrJoint values[26];
    struct AlvrSpaceRelation global_hand_relation;
    bool is_active;
} AlvrJointSet;

enum AlvrOutput_Tag
#ifdef __cplusplus
  : uint8_t
#endif // __cplusplus
 {
    ALVR_OUTPUT_HAPTICS,
};
#ifndef __cplusplus
typedef uint8_t AlvrOutput_Tag;
#endif // __cplusplus

typedef struct Haptics_Body {
    AlvrOutput_Tag tag;
    float frequency;
    float amplitude;
    uint64_t duration_ns;
} Haptics_Body;

typedef union AlvrOutput {
    AlvrOutput_Tag tag;
    Haptics_Body HAPTICS;
} AlvrOutput;

typedef struct AlvrFov {
    // Negative, radians
    float left;
    // Positive, radians
    float right;
    // Positive, radians
    float up;
    // Negative, radians
    float down;
} AlvrFov;

typedef struct AlvrVkInfo {
    uint64_t instance;
    uint32_t version;
    uint64_t physical_device;
    uint32_t physical_device_index;
    uint64_t device;
    uint32_t queue_family_index;
    uint32_t queue_index;
    uint64_t queue;
    uint64_t queue_mutex;
} AlvrVkInfo;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// # Safety
void *HmdDriverFactory(const char *interface_name, int32_t *return_code);

uint64_t alvr_get_time_ns(void);

uint64_t alvr_path_to_id(const char *path_string);

void alvr_initialize(struct AlvrTargetConfig *out_target_config);

bool alvr_poll_event(struct AlvrEvent *out_event);

void alvr_shutdown(void);

uint64_t alvr_get_devices(struct AlvrDeviceConfig *out_device_configs);

void alvr_update_inputs(uint64_t device_id);

uint64_t alvr_get_inputs(uint64_t device_id,
                         struct AlvrInput *out_inputs_arr,
                         uint64_t out_timestamp_ns);

void alvr_get_tracked_pose(uint64_t pose_id,
                           uint64_t timestamp_ns,
                           struct AlvrSpaceRelation *out_relation);

void alvr_get_hand_tracking(uint64_t device_id,
                            uint64_t timestamp_ns,
                            struct AlvrJointSet *out_joint_set);

void alvr_set_output(uint64_t output_id, const union AlvrOutput *value);

void alvr_view_poses(struct AlvrSpaceRelation *out_head_relation,
                     struct AlvrFov *out_fov_arr,
                     struct AlvrPose *out_relative_pose_arr);

void alvr_destroy_device(uint64_t device_id);

bool alvr_init_vulkan(struct AlvrVkInfo *info);

void alvr_take_vulkan(AlvrVkExport *info);

float alvr_get_framerate(void);

void alvr_pre_vulkan(uint64_t timeline_sem_val, uint32_t img_idx);

void alvr_post_vulkan(void);

void alvr_create_vk_target_swapchain(uint32_t width,
                                     uint32_t height,
                                     int32_t vk_color_format,
                                     int32_t vk_color_space,
                                     uint32_t vk_image_usage,
                                     int32_t vk_present_mode,
                                     uint64_t image_count);

int32_t alvr_acquire_image(uint64_t out_swapchain_index);

int32_t alvr_present(uint64_t vk_queue,
                     uint64_t swapchain_index,
                     uint64_t timeline_semaphore_value,
                     uint64_t timestamp_ns);

void alvr_destroy_vk_target_swapchain(void);

uint64_t get_serial_number(uint64_t device_id, char *out_str);

void set_device_openvr_props(uint64_t device_id);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
