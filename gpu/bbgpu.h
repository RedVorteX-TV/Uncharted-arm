/* C interface of the GPU library (gpu/): shadPS4's Liverpool/Vulkan video core,
 * GnmDriver, VideoOut and kernel event queues, adapted to the native loader. */
#ifndef BBGPU_H
#define BBGPU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char *title;          /* window title */
    const char *serial;         /* CUSA id, names the pipeline cache */
    const char *user_dir;       /* pipeline cache/logs directory */
    uint32_t sdk_version;       /* from the eboot's procparam */
    uint32_t psf_attributes;    /* param.sfo ATTRIBUTE */
    int32_t width, height;      /* initial window size */
} BbGpuConfig;
/* Registers kernel event queues (needed with or without graphics). */
void bbgpu_register_kernel(void);
/* Co-op: the game folder and param.sfo identity for the network and PSN libraries. */
void bbnet_configure(const char *app0, const char *serial, const char *title, const char *app_ver);
/* Creates window, Vulkan device, presenter and GPU command processor. */
int bbgpu_init(const BbGpuConfig *config);
/* Writes the Vulkan driver's pipeline cache to disk (before the process ends or restarts). */
void bbgpu_flush_pipeline_cache(void);
/* Function for an imported NID ("NID#lib#mod"), or 0 when the GPU library does not provide it. */
uintptr_t bbgpu_resolve(const char *scoped_nid);
/* Called first by the loader's SIGSEGV handler: 1 when a GPU page-tracking fault was handled. */
int bbgpu_handle_fault(void *ucontext, void *address);
/* BB_WRITE_LOG=1: prints the logged GPU-side writes to guest memory near the fault. */
void bbgpu_dump_guest_writes(void *ucontext);
/* Keyboard text entry through the game window (IME dialog). begin returns 0 when
 * no window exists; poll returns 0 typing, 1 confirmed, 2 cancelled (UTF-8 text). */
int bbgpu_text_input_begin(const char *initial_utf8, const char *prompt_utf8);
int bbgpu_text_input_poll(char *out_utf8, uint64_t size);
/* 1 while the in-game settings menu is open: the game's pad input is held neutral. */
int bbgpu_overlay_captures_input(void);
/* Android (no SDL window): the pad's buttons for the in-game menu, every input poll (runtime_pad.c,
 * BB_PAD_FILE). L3+R3 opens it as on the desktop. */
enum {
    BB_OVERLAY_PAD_SOUTH = 1u << 0, BB_OVERLAY_PAD_EAST = 1u << 1, BB_OVERLAY_PAD_WEST = 1u << 2,
    BB_OVERLAY_PAD_NORTH = 1u << 3, BB_OVERLAY_PAD_UP = 1u << 4, BB_OVERLAY_PAD_DOWN = 1u << 5,
    BB_OVERLAY_PAD_LEFT = 1u << 6, BB_OVERLAY_PAD_RIGHT = 1u << 7, BB_OVERLAY_PAD_L1 = 1u << 8,
    BB_OVERLAY_PAD_R1 = 1u << 9, BB_OVERLAY_PAD_START = 1u << 10, BB_OVERLAY_PAD_BACK = 1u << 11,
    BB_OVERLAY_PAD_L3 = 1u << 12, BB_OVERLAY_PAD_R3 = 1u << 13,
};
void bbgpu_overlay_pad(uint32_t buttons);
/* Mouse controls: look=1 captures the cursor while the game window has the focus (relative
 * mode). sample returns the motion (pixels) and wheel steps since the last call and the held
 * buttons (1 left, 2 right, 4 middle, 8 X1, 16 X2); 1 while the cursor is captured. */
void bbgpu_mouse_configure(int look);
/* Heading (degrees, about the vertical axis) of the camera the game last rendered with and how old
 * it is; 0 before the first frame with a known camera. */
int bbgpu_camera_yaw(float *yaw_degrees, double *age_seconds);
int bbgpu_mouse_sample(float *dx, float *dy, uint32_t *buttons, int *wheel);
/* Number of symbols registered by the vendored libraries (diagnostics). */
unsigned bbgpu_symbol_count(void);
#ifdef __ANDROID__
/* ADPF performance hints (shim/bbport_adpf.cpp): the game's main thread, a host thread named. */
void bbgpu_adpf_register_main(void);
void bbgpu_adpf_thread_named(const char* name);
#endif
#ifdef __cplusplus
}
#endif
#endif
