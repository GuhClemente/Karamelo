#![allow(dead_code, unused_variables, unused_imports, non_snake_case, non_camel_case_types)]

use std::ffi::{c_char, c_float, c_uint, c_void};
use std::sync::Mutex;
use std::sync::atomic::Ordering;
use crate::device::{self, Device};
use crate::ui;

pub const RETRO_API_VERSION: c_uint = 1;

pub const RETRO_DEVICE_NONE: c_uint = 0;
pub const RETRO_DEVICE_JOYPAD: c_uint = 1;
pub const RETRO_DEVICE_ANALOG: c_uint = 5;

pub const RETRO_DEVICE_ID_JOYPAD_B: c_uint = 0;
pub const RETRO_DEVICE_ID_JOYPAD_Y: c_uint = 1;
pub const RETRO_DEVICE_ID_JOYPAD_SELECT: c_uint = 2;
pub const RETRO_DEVICE_ID_JOYPAD_START: c_uint = 3;
pub const RETRO_DEVICE_ID_JOYPAD_UP: c_uint = 4;
pub const RETRO_DEVICE_ID_JOYPAD_DOWN: c_uint = 5;
pub const RETRO_DEVICE_ID_JOYPAD_LEFT: c_uint = 6;
pub const RETRO_DEVICE_ID_JOYPAD_RIGHT: c_uint = 7;
pub const RETRO_DEVICE_ID_JOYPAD_A: c_uint = 8;
pub const RETRO_DEVICE_ID_JOYPAD_X: c_uint = 9;
pub const RETRO_DEVICE_ID_JOYPAD_L: c_uint = 10;
pub const RETRO_DEVICE_ID_JOYPAD_R: c_uint = 11;
pub const RETRO_DEVICE_ID_JOYPAD_L2: c_uint = 12; // Z trigger
pub const RETRO_DEVICE_ID_JOYPAD_R2: c_uint = 13;
pub const RETRO_DEVICE_ID_JOYPAD_L3: c_uint = 14;
pub const RETRO_DEVICE_ID_JOYPAD_R3: c_uint = 15;

pub const RETRO_DEVICE_INDEX_ANALOG_LEFT: c_uint = 0;
pub const RETRO_DEVICE_INDEX_ANALOG_RIGHT: c_uint = 1;
pub const RETRO_DEVICE_ID_ANALOG_X: c_uint = 0;
pub const RETRO_DEVICE_ID_ANALOG_Y: c_uint = 1;

pub const RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: c_uint = 10;
pub const RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: c_uint = 32;
pub const RETRO_ENVIRONMENT_SET_GEOMETRY: c_uint = 37;

pub const RETRO_PIXEL_FORMAT_0RGB1555: c_uint = 0;
pub const RETRO_PIXEL_FORMAT_XRGB8888: c_uint = 1;
pub const RETRO_PIXEL_FORMAT_RGB565: c_uint = 2;

pub const RETRO_MEMORY_SAVE_RAM: c_uint = 0;
pub const RETRO_MEMORY_RTC: c_uint = 1;
pub const RETRO_MEMORY_SYSTEM_RAM: c_uint = 2;

pub type retro_environment_t = unsafe extern "C" fn(cmd: c_uint, data: *mut c_void) -> bool;
pub type retro_video_refresh_t = unsafe extern "C" fn(data: *const c_void, width: c_uint, height: c_uint, pitch: usize);
pub type retro_audio_sample_t = unsafe extern "C" fn(left: i16, right: i16);
pub type retro_audio_sample_batch_t = unsafe extern "C" fn(data: *const i16, frames: usize) -> usize;
pub type retro_input_poll_t = unsafe extern "C" fn();
pub type retro_input_state_t = unsafe extern "C" fn(port: c_uint, device: c_uint, index: c_uint, id: c_uint) -> i16;

#[repr(C)]
pub struct retro_system_info {
    pub library_name: *const c_char,
    pub library_version: *const c_char,
    pub valid_extensions: *const c_char,
    pub need_fullpath: bool,
    pub block_extract: bool,
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct retro_game_geometry {
    pub base_width: c_uint,
    pub base_height: c_uint,
    pub max_width: c_uint,
    pub max_height: c_uint,
    pub aspect_ratio: c_float,
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct retro_system_timing {
    pub fps: f64,
    pub sample_rate: f64,
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct retro_system_av_info {
    pub geometry: retro_game_geometry,
    pub timing: retro_system_timing,
}

#[repr(C)]
pub struct retro_game_info {
    pub path: *const c_char,
    pub data: *const c_void,
    pub size: usize,
    pub meta: *const c_char,
}

static ENV_CB: Mutex<Option<retro_environment_t>> = Mutex::new(None);
static INPUT_POLL_CB: Mutex<Option<retro_input_poll_t>> = Mutex::new(None);
static INPUT_STATE_CB: Mutex<Option<retro_input_state_t>> = Mutex::new(None);
static G_DEVICE: Mutex<Option<Box<Device>>> = Mutex::new(None);
static RUNTIME: Mutex<Option<tokio::runtime::Runtime>> = Mutex::new(None);

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_api_version() -> c_uint {
    RETRO_API_VERSION
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_set_environment(cb: retro_environment_t) {
    *ENV_CB.lock().unwrap() = Some(cb);
    let mut fmt: c_uint = RETRO_PIXEL_FORMAT_XRGB8888;
    cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &mut fmt as *mut _ as *mut c_void);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_set_video_refresh(cb: retro_video_refresh_t) {
    *ui::video::VIDEO_REFRESH_CB.lock().unwrap() = Some(cb);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_set_audio_sample(_cb: retro_audio_sample_t) {
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_set_audio_sample_batch(cb: retro_audio_sample_batch_t) {
    *ui::audio::AUDIO_BATCH_CB.lock().unwrap() = Some(cb);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_set_input_poll(cb: retro_input_poll_t) {
    *INPUT_POLL_CB.lock().unwrap() = Some(cb);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_set_input_state(cb: retro_input_state_t) {
    *INPUT_STATE_CB.lock().unwrap() = Some(cb);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_set_controller_port_device(_port: c_uint, _device: c_uint) {
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_get_system_info(info: *mut retro_system_info) {
    if info.is_null() {
        return;
    }
    (*info).library_name = b"Gopher64 (MiSTer 4 All)\0".as_ptr() as *const c_char;
    (*info).library_version = b"1.1.36\0".as_ptr() as *const c_char;
    (*info).valid_extensions = b"z64|n64|v64\0".as_ptr() as *const c_char;
    (*info).need_fullpath = false;
    (*info).block_extract = false;
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_get_system_av_info(av_info: *mut retro_system_av_info) {
    if av_info.is_null() {
        return;
    }
    let is_pal = if let Ok(guard) = G_DEVICE.lock() {
        if let Some(dev) = guard.as_ref() {
            dev.cart.pal
        } else {
            false
        }
    } else {
        false
    };

    (*av_info).geometry.base_width = 320;
    (*av_info).geometry.base_height = if is_pal { 288 } else { 240 };
    (*av_info).geometry.max_width = 640;
    (*av_info).geometry.max_height = if is_pal { 576 } else { 480 };
    (*av_info).geometry.aspect_ratio = 4.0 / 3.0;

    (*av_info).timing.fps = if is_pal { 50.0 } else { 60.0 };
    (*av_info).timing.sample_rate = 44100.0;
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_init() {
    ui::video::LIBRETRO_MODE.store(true, Ordering::Relaxed);
    let mut rt_guard = RUNTIME.lock().unwrap();
    if rt_guard.is_none() {
        if let Ok(rt) = tokio::runtime::Builder::new_multi_thread().enable_all().build() {
            *rt_guard = Some(rt);
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_deinit() {
    retro_unload_game();
    let mut rt_guard = RUNTIME.lock().unwrap();
    *rt_guard = None;
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_load_game(game: *const retro_game_info) -> bool {
    let result = std::panic::catch_unwind(|| {
        if game.is_null() {
            return false;
        }
        let game_ref = unsafe { &*game };
        if game_ref.data.is_null() || game_ref.size == 0 {
            return false;
        }

        ui::video::LIBRETRO_MODE.store(true, Ordering::Relaxed);

        // Ensure Tokio runtime is active
        retro_init();

        let raw_slice = unsafe { std::slice::from_raw_parts(game_ref.data as *const u8, game_ref.size) };
        let rom_contents = match device::swap_rom(raw_slice.to_vec()) {
            Some(c) => c,
            None => raw_slice.to_vec(),
        };

        let mut dev = Device::new(false);

        // 1. Initialize ROM & Cartridge
        device::cart::rom::init(&mut dev, &rom_contents);

        // 2. Initialize RDRAM
        device::rdram::init(&mut dev);

        // 3. Initialize Headless Video (Parallel-RDP Vulkan)
        ui::video::init(&mut dev, false);

        // 4. Initialize Audio & Input
        ui::audio::init(&mut dev);
        ui::input::init(&mut dev.ui);

        // 5. Initialize RNG & RTC timing
        device::init_rng_rtc(&mut dev);

        // 6. Initialize N64 subcomponents in exact order
        device::mi::init(&mut dev);
        device::pif::init(&mut dev);
        device::memory::init(&mut dev);
        device::cache::init(&mut dev);
        device::rsp_interface::init(&mut dev);
        device::rdp::init(&mut dev);
        device::vi::init(&mut dev);
        device::cpu::init(&mut dev);

        // 7. Initialize Save Storage and format EEPROM/SRAM/Flash/Mempak
        ui::storage::init(&mut dev.ui, &dev.cart.rom);
        ui::storage::format_saves(&mut dev);

        *G_DEVICE.lock().unwrap() = Some(dev);
        true
    });

    match result {
        Ok(ok) => ok,
        Err(err) => {
            eprintln!("[Gopher64] Panic during retro_load_game: {:?}", err);
            false
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_unload_game() {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(mut guard) = G_DEVICE.lock() {
            if let Some(mut dev) = guard.take() {
                ui::video::close(&dev.ui);
                ui::audio::close(&mut dev.ui);
            }
        }
    });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_reset() {
    let _ = std::panic::catch_unwind(|| {
        if let Ok(mut guard) = G_DEVICE.lock() {
            if let Some(dev) = guard.as_mut() {
                device::cpu::init(dev);
                device::rsp_interface::init(dev);
                device::rdp::init(dev);
                device::vi::init(dev);
            }
        }
    });
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_run() {
    let _ = std::panic::catch_unwind(|| {
        let mut guard = match G_DEVICE.lock() {
            Ok(g) => g,
            Err(_) => return,
        };
        let Some(dev) = guard.as_mut() else {
            return;
        };

        // Poll controllers
        let poll_opt = *INPUT_POLL_CB.lock().unwrap();
        if let Some(poll_cb) = poll_opt {
            unsafe { poll_cb(); }
        }

        let state_opt = *INPUT_STATE_CB.lock().unwrap();
        if let Some(state_cb) = state_opt {
            for port in 0..4 {
                let mut keys: u32 = 0;
                unsafe {
                    // D-Pad
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT) != 0 { keys |= 1 << 0; }
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT) != 0 { keys |= 1 << 1; }
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN) != 0 { keys |= 1 << 2; }
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP) != 0 { keys |= 1 << 3; }
                    // Start, Z, B, A
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) != 0 { keys |= 1 << 4; }
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2) != 0 { keys |= 1 << 5; } // Z trigger
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y) != 0 { keys |= 1 << 6; } // B
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) != 0 { keys |= 1 << 7; } // A
                    // C-buttons (Right analog stick or RetroPad buttons)
                    let rx = state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
                    let ry = state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
                    if rx > 16384 || state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) != 0 { keys |= 1 << 8; } // C-Right
                    if rx < -16384 || state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X) != 0 { keys |= 1 << 9; } // C-Left
                    if ry > 16384 || state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2) != 0 { keys |= 1 << 10; } // C-Down
                    if ry < -16384 { keys |= 1 << 11; } // C-Up
                    // R, L triggers
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R) != 0 { keys |= 1 << 12; } // R
                    if state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L) != 0 { keys |= 1 << 13; } // L
                    // Analog Stick (Left analog)
                    let ax = state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
                    let ay = state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
                    let norm_x = (ax as f64 * 85.0 / 32767.0).round() as i8;
                    let norm_y = (-ay as f64 * 85.0 / 32767.0).round() as i8;
                    keys |= (norm_x as u8 as u32) << 16;
                    keys |= (norm_y as u8 as u32) << 24;
                }

                ui::input::LIBRETRO_INPUT[port as usize].store(keys, Ordering::Relaxed);
            }
        }

        // Step CPU until vertical interrupt finishes frame
        device::cpu::run(dev);
    });
}

// RetroAchievements Direct Memory Access
#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_get_memory_data(id: c_uint) -> *mut c_void {
    match id {
        RETRO_MEMORY_SYSTEM_RAM => {
            if let Ok(mut guard) = G_DEVICE.lock() {
                if let Some(dev) = guard.as_mut() {
                    dev.rdram.mem.as_mut_ptr() as *mut c_void
                } else {
                    std::ptr::null_mut()
                }
            } else {
                std::ptr::null_mut()
            }
        }
        _ => std::ptr::null_mut(),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_get_memory_size(id: c_uint) -> usize {
    match id {
        RETRO_MEMORY_SYSTEM_RAM => {
            if let Ok(guard) = G_DEVICE.lock() {
                if let Some(dev) = guard.as_ref() {
                    dev.rdram.size as usize
                } else {
                    0
                }
            } else {
                0
            }
        }
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_serialize_size() -> usize {
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_serialize(_data: *mut c_void, _size: usize) -> bool {
    false
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn retro_unserialize(_data: *const c_void, _size: usize) -> bool {
    false
}
