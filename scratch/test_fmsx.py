import ctypes
import os
import sys

dll = ctypes.CDLL(os.path.abspath('cores/msx.dll'))

RETRO_ENVIRONMENT_SET_PIXEL_FORMAT = 10
RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY = 9
RETRO_ENVIRONMENT_GET_VARIABLE = 15
RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE = 17
RETRO_ENVIRONMENT_GET_LOG_INTERFACE = 27

class RetroVariable(ctypes.Structure):
    _fields_ = [('key', ctypes.c_char_p), ('value', ctypes.c_char_p)]

class RetroGameInfo(ctypes.Structure):
    _fields_ = [('path', ctypes.c_char_p), ('data', ctypes.c_void_p), ('size', ctypes.c_size_t), ('meta', ctypes.c_char_p)]

ENV_CB = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_uint, ctypes.c_void_p)
LOG_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint, ctypes.c_char_p)
VIDEO_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_size_t)
AUDIO_SAMPLE_CB = ctypes.CFUNCTYPE(None, ctypes.c_int16, ctypes.c_int16)
AUDIO_BATCH_CB = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t)
POLL_CB = ctypes.CFUNCTYPE(None)
INPUT_CB = ctypes.CFUNCTYPE(ctypes.c_int16, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint)

class RetroLogCallback(ctypes.Structure):
    _fields_ = [('log', LOG_CB)]

@LOG_CB
def log_func(level, fmt):
    try:
        msg = fmt.decode('utf-8', 'replace') if fmt else ''
    except:
        msg = str(fmt)
    print(f'[fMSX log {level}] {msg}', flush=True)

log_obj = RetroLogCallback(log_func)

target_mode = b'MSX2'
target_mapper = b'Guess'
defaults = {}

@ENV_CB
def env_func(cmd, data):
    global target_mode, target_mapper, defaults
    if cmd == 16: # RETRO_ENVIRONMENT_SET_VARIABLES
        p = ctypes.cast(data, ctypes.POINTER(RetroVariable))
        i = 0
        while p[i].key is not None:
            k = p[i].key
            v = p[i].value
            if b'; ' in v:
                opts = v.split(b'; ')[1]
                default_opt = opts.split(b'|')[0]
                defaults[k] = default_opt
            i += 1
        return True
    if cmd == RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        p = ctypes.cast(data, ctypes.POINTER(RetroLogCallback))
        p.contents.log = log_func
        return True
    if cmd == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        p = ctypes.cast(data, ctypes.POINTER(ctypes.c_char_p))
        p[0] = (os.path.abspath('app/bios') + '/').encode('utf-8')
        return True
    if cmd == RETRO_ENVIRONMENT_GET_VARIABLE:
        v = ctypes.cast(data, ctypes.POINTER(RetroVariable)).contents
        if v.key == b'fmsx_mode':
            v.value = target_mode
            return True
        if v.key == b'fmsx_mapper_type_mode':
            v.value = target_mapper
            return True
        if v.key == b'fmsx_log_level':
            v.value = b'Debug'
            return True
        if v.key in defaults:
            v.value = defaults[v.key]
            return True
        return False
    if cmd == RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        p = ctypes.cast(data, ctypes.POINTER(ctypes.c_bool))
        p[0] = False
        return True
    if cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return True
    return False

nonblack = 0
last_frame_nz = 0
@VIDEO_CB
def video_func(d, w, h, p):
    global nonblack, last_frame_nz
    if d:
        buf = ctypes.string_at(d, p*h)
        nz = sum(1 for b in buf if b != 0)
        last_frame_nz = nz
        if nz > 0:
            nonblack += 1

@AUDIO_SAMPLE_CB
def audio_sample_func(l, r): pass

@AUDIO_BATCH_CB
def audio_batch_func(d, f): return f

@POLL_CB
def poll_func(): pass

@INPUT_CB
def input_func(p, d, i, id): return 0

dll.retro_set_environment.argtypes = [ENV_CB]
dll.retro_set_environment.restype = None
dll.retro_set_video_refresh.argtypes = [VIDEO_CB]
dll.retro_set_video_refresh.restype = None
dll.retro_set_audio_sample.argtypes = [AUDIO_SAMPLE_CB]
dll.retro_set_audio_sample.restype = None
dll.retro_set_audio_sample_batch.argtypes = [AUDIO_BATCH_CB]
dll.retro_set_audio_sample_batch.restype = None
dll.retro_set_input_poll.argtypes = [POLL_CB]
dll.retro_set_input_poll.restype = None
dll.retro_set_input_state.argtypes = [INPUT_CB]
dll.retro_set_input_state.restype = None
dll.retro_init.argtypes = []
dll.retro_init.restype = None
dll.retro_deinit.argtypes = []
dll.retro_deinit.restype = None
dll.retro_load_game.argtypes = [ctypes.POINTER(RetroGameInfo)]
dll.retro_load_game.restype = ctypes.c_bool
dll.retro_unload_game.argtypes = []
dll.retro_unload_game.restype = None
dll.retro_run.argtypes = []
dll.retro_run.restype = None

dll.retro_set_environment(env_func)
dll.retro_init()
dll.retro_set_video_refresh(video_func)
dll.retro_set_audio_sample(audio_sample_func)
dll.retro_set_audio_sample_batch(audio_batch_func)
dll.retro_set_input_poll(poll_func)
dll.retro_set_input_state(input_func)

rom_path = os.path.abspath('cache/Aleste - Compile (1988) [English] [Translated] [3427].rom')
with open(rom_path, 'rb') as f:
    rom_data = f.read()

rom_buf = ctypes.create_string_buffer(rom_data)
rom_path_bytes = rom_path.encode('utf-8')

mapper_mode = sys.argv[1].encode('utf-8') if len(sys.argv) > 1 else b'Guess'
msx_mode = sys.argv[2].encode('utf-8') if len(sys.argv) > 2 else b'MSX2'

target_mapper = mapper_mode
target_mode = msx_mode
nonblack = 0
ginfo = RetroGameInfo(rom_path_bytes, ctypes.cast(rom_buf, ctypes.c_void_p), len(rom_data), None)
ok = dll.retro_load_game(ctypes.byref(ginfo))
print(f'=== Testing mapper={mapper_mode.decode()} mode={msx_mode.decode()} load={ok} ===', flush=True)
for frame in range(300):
    dll.retro_run()
    if last_frame_nz > 0 and nonblack <= 10:
        print(f'  Frame {frame+1}: nz={last_frame_nz} nonblack_total={nonblack}', flush=True)
print(f'Done 300 frames. nonblack={nonblack}', flush=True)
dll.retro_unload_game()
dll.retro_deinit()
