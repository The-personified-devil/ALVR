use std::ffi::{c_char, CStr};
use std::io::Write;
use crate::*;

use alvr_common::log::{self, log};

use crate::c_api::AlvrVkInfo;

// TODO: Fully replace this with the thing from zarik
pub fn monado_entry(info: *mut AlvrVkInfo) -> bool {
    crate::init();

    unsafe {
        crate::FRAME_RENDER_VS_CSO_PTR = crate::FRAME_RENDER_VS_CSO.as_ptr();
        crate::FRAME_RENDER_VS_CSO_LEN = crate::FRAME_RENDER_VS_CSO.len() as _;
        crate::FRAME_RENDER_PS_CSO_PTR = crate::FRAME_RENDER_PS_CSO.as_ptr();
        crate::FRAME_RENDER_PS_CSO_LEN = crate::FRAME_RENDER_PS_CSO.len() as _;
        crate::QUAD_SHADER_CSO_PTR = crate::QUAD_SHADER_CSO.as_ptr();
        crate::QUAD_SHADER_CSO_LEN = crate::QUAD_SHADER_CSO.len() as _;
        crate::COMPRESS_AXIS_ALIGNED_CSO_PTR = crate::COMPRESS_AXIS_ALIGNED_CSO.as_ptr();
        crate::COMPRESS_AXIS_ALIGNED_CSO_LEN = crate::COMPRESS_AXIS_ALIGNED_CSO.len() as _;
        crate::COLOR_CORRECTION_CSO_PTR = crate::COLOR_CORRECTION_CSO.as_ptr();
        crate::COLOR_CORRECTION_CSO_LEN = crate::COLOR_CORRECTION_CSO.len() as _;
        crate::RGBTOYUV420_CSO_PTR = crate::RGBTOYUV420_CSO.as_ptr();
        crate::RGBTOYUV420_CSO_LEN = crate::RGBTOYUV420_CSO.len() as _;
        crate::QUAD_SHADER_COMP_SPV_PTR = crate::QUAD_SHADER_COMP_SPV.as_ptr();
        crate::QUAD_SHADER_COMP_SPV_LEN = crate::QUAD_SHADER_COMP_SPV.len() as _;
        crate::COLOR_SHADER_COMP_SPV_PTR = crate::COLOR_SHADER_COMP_SPV.as_ptr();
        crate::COLOR_SHADER_COMP_SPV_LEN = crate::COLOR_SHADER_COMP_SPV.len() as _;
        crate::FFR_SHADER_COMP_SPV_PTR = crate::FFR_SHADER_COMP_SPV.as_ptr();
        crate::FFR_SHADER_COMP_SPV_LEN = crate::FFR_SHADER_COMP_SPV.len() as _;
        crate::RGBTOYUV420_SHADER_COMP_SPV_PTR = crate::RGBTOYUV420_SHADER_COMP_SPV.as_ptr();
        crate::RGBTOYUV420_SHADER_COMP_SPV_LEN = crate::RGBTOYUV420_SHADER_COMP_SPV.len() as _;
    }

    unsafe extern "C" fn log_error(string_ptr: *const c_char) {
        alvr_common::show_e(CStr::from_ptr(string_ptr).to_string_lossy());
    }

    unsafe fn log(level: log::Level, string_ptr: *const c_char) {
        log::log!(level, "{}", CStr::from_ptr(string_ptr).to_string_lossy());
    }

    unsafe extern "C" fn log_warn(string_ptr: *const c_char) {
        log(log::Level::Warn, string_ptr);
    }

    unsafe extern "C" fn log_info(string_ptr: *const c_char) {
        log(log::Level::Info, string_ptr);
    }

    unsafe extern "C" fn log_debug(string_ptr: *const c_char) {
        log(log::Level::Debug, string_ptr);
    }

    unsafe {
        crate::LogError = Some(log_error);
        crate::LogWarn = Some(log_warn);
        crate::LogInfo = Some(log_info);
        crate::LogDebug = Some(log_debug);
    }

    extern "C" fn get_dynamic_encoder_params() -> crate::FfiDynamicEncoderParams {
        // let (params, stats) = {
        //     let server_data_lock = crate::SERVER_DATA_MANAGER.read();
        //     crate::BITRATE_MANAGER
        //         .lock()
        //         .get_encoder_params(&server_data_lock.settings().video.bitrate)
        // };

        // if let Some(stats) = stats {
        //     if let Some(stats_manager) = &mut *crate::STATISTICS_MANAGER.lock() {
        //         stats_manager.report_nominal_bitrate_stats(stats);
        //     }
        // }

        // params
        crate::FfiDynamicEncoderParams {
            updated: 0,
            bitrate_bps: 0,
            framerate: 0.,
        }
    }

    extern "C" fn set_video_config_nals(buffer_ptr: *const u8, len: i32, codec: i32) {
        let codec = if codec == 0 {
            crate::CodecType::H264
        } else if codec == 1 {
            crate::CodecType::Hevc
        } else {
            crate::CodecType::AV1
        };

        let mut config_buffer = vec![0; len as usize];

        unsafe {
            core::ptr::copy_nonoverlapping(buffer_ptr, config_buffer.as_mut_ptr(), len as usize)
        };

        if let Some(sender) = &*crate::VIDEO_MIRROR_SENDER.lock() {
            sender.send(config_buffer.clone()).ok();
        }

        if let Some(file) = &mut *crate::VIDEO_RECORDING_FILE.lock() {
            file.write_all(&config_buffer).ok();
        }

        *crate::DECODER_CONFIG.lock() = Some(crate::DecoderInitializationConfig {
            codec,
            config_buffer,
        });
    }

    pub extern "C" fn driver_ready_idle(set_default_chap: bool) {
        // Note: Idle state is not used on the server side
        *crate::LIFECYCLE_STATE.write() = crate::LifecycleState::Resumed;

        crate::thread::spawn(move || {
            // if set_default_chap {
            //     // call this when inside a new thread. Calling this on the parent thread will crash
            //     // SteamVR
            //     unsafe {
            //         InitOpenvrClient();
            //         SetChaperoneArea(2.0, 2.0);
            //         ShutdownOpenvrClient();
            //     }
            // }

            crate::connection::handshake_loop();
        });
    }

    unsafe extern "C" fn log_periodically(tag_ptr: *const c_char, message_ptr: *const c_char) {
        const INTERVAL: Duration = Duration::from_secs(1);
        static LASTEST_TAG_TIMESTAMPS: Lazy<Mutex<HashMap<String, Instant>>> =
            Lazy::new(|| Mutex::new(HashMap::new()));

        let tag = CStr::from_ptr(tag_ptr).to_string_lossy();
        let message = CStr::from_ptr(message_ptr).to_string_lossy();

        let mut timestamps_ref = LASTEST_TAG_TIMESTAMPS.lock();
        let old_timestamp = timestamps_ref
            .entry(tag.to_string())
            .or_insert_with(Instant::now);
        if *old_timestamp + INTERVAL < Instant::now() {
            *old_timestamp += INTERVAL;

            log::warn!("{}: {}", tag, message);
        }
    }

    unsafe extern "C" fn path_string_to_hash(path: *const c_char) -> u64 {
        alvr_common::hash_string(CStr::from_ptr(path).to_str().unwrap())
    }

    extern "C" fn report_present(timestamp_ns: u64, offset_ns: u64) {
        if let Some(stats) = &mut *STATISTICS_MANAGER.lock() {
            stats.report_frame_present(
                Duration::from_nanos(timestamp_ns),
                Duration::from_nanos(offset_ns),
            );
        }

        let server_data_lock = SERVER_DATA_MANAGER.read();
        BITRATE_MANAGER
            .lock()
            .report_frame_present(&server_data_lock.settings().video.bitrate.adapt_to_framerate);
    }

    extern "C" fn report_composed(timestamp_ns: u64, offset_ns: u64) {
        if let Some(stats) = &mut *STATISTICS_MANAGER.lock() {
            stats.report_frame_composed(
                Duration::from_nanos(timestamp_ns),
                Duration::from_nanos(offset_ns),
            );
        }
    }

    extern "C" fn wait_for_vsync() {
        if SERVER_DATA_MANAGER
            .read()
            .settings()
            .video
            .optimize_game_render_latency
        {
            // Note: unlock STATISTICS_MANAGER as soon as possible
            let wait_duration = STATISTICS_MANAGER
                .lock()
                .as_mut()
                .map(|stats| stats.duration_until_next_vsync());

            if let Some(duration) = wait_duration {
                thread::sleep(duration);
            }
        }
    }


    unsafe {
        crate::SetVideoConfigNals = Some(set_video_config_nals);
        crate::GetDynamicEncoderParams = Some(get_dynamic_encoder_params);
        crate::VideoSend = Some(crate::connection::send_video);
        crate::DriverReadyIdle = Some(driver_ready_idle);

    LogError = Some(log_error);
    LogWarn = Some(log_warn);
    LogInfo = Some(log_info);
    LogDebug = Some(log_debug);
    LogPeriodically = Some(log_periodically);
    DriverReadyIdle = Some(driver_ready_idle);
    SetVideoConfigNals = Some(set_video_config_nals);
    VideoSend = Some(connection::send_video);
    HapticsSend = Some(connection::send_haptics);
    ShutdownRuntime = Some(shutdown_driver);
    PathStringToHash = Some(path_string_to_hash);
    ReportPresent = Some(report_present);
    ReportComposed = Some(report_composed);
    GetSerialNumber = Some(openvr_props::get_serial_number);
    SetOpenvrProps = Some(openvr_props::set_device_openvr_props);
    RegisterButtons = Some(input_mapping::register_buttons);
    GetDynamicEncoderParams = Some(get_dynamic_encoder_params);
    WaitForVSync = Some(wait_for_vsync);

    }

    true
}
