#pragma once

/* IPC messages between window-manager clients (Syscalls.c) and the window
 * manager (com.ImplusOS.windowmanager/IPC/WM_IPC.c). Every message starts
 * with a wm_msg_header_t. */

#include <stdint.h>
#include <stdbool.h>

/* client -> WM */
#define WM_CREATE_WINDOW        1
#define WM_DESTROY_WINDOW       2
#define WM_SET_WINDOW_RECT      3
#define WM_SHOW_WINDOW          4
#define WM_RAISE_WINDOW         6
#define WM_SET_FOCUS            8
/* Shell chrome rather than an application: no frame, not on the taskbar.
 * Payload: bool. */
#define WM_SET_WINDOW_SYSTEM   15
#define WM_SUBSCRIBE_INPUT     30
#define WM_GET_WINDOW_RECT     40
#define WM_SHOW_NOTIFICATION   80
#define WM_SHOW_DIALOG         81
/* Payload: uint32_t accent, uint32_t dark. */
#define WM_SET_THEME           90
#define WM_RELOAD_BACKGROUND   91
#define WM_GET_BACKING_STORE  100
#define WM_DAMAGE             102
#define WM_SET_WINDOW_SURFACE_OPAQUE 112
/* Payload: wm_set_title_t (UTF-8, NUL-terminated within the buffer). */
#define WM_SET_WINDOW_TITLE   113
/* The owner handles this window's close button itself: the compositor sends
 * it WM_CLOSE_REQUESTED instead of destroying the window, and never kills the
 * owner when its last such window goes away. Payload: bool. For clients that
 * host other programs' windows (Userland/API/Source/XSession.c). */
#define WM_SET_WINDOW_CLOSE_POLICY 114
/* Screen rectangle of the content area (below the title bar). The reply has
 * the same layout as WM_GET_WINDOW_RECT's. */
#define WM_GET_CONTENT_RECT   116

/* WM -> client */
#define WM_KEYBOARD_EVENT      20
#define WM_MOUSE_EVENT         21
#define WM_WINDOW_CREATED      41
#define WM_WINDOW_DESTROYED    42
#define WM_WINDOW_RESIZED      44
#define WM_BACKING_STORE_READY 101
/* The user asked to close header.window_id. */
#define WM_CLOSE_REQUESTED    115

#define WM_STATUS_OK             0
#define WM_STATUS_INVALID_ARG  (-22)
#define WM_STATUS_NOT_FOUND    (-2)
#define WM_STATUS_DENIED       (-13)
#define WM_STATUS_UNSUPPORTED  (-95)

typedef struct {
    uint32_t type;
    uint32_t request_id;
    uint32_t window_id;
} wm_msg_header_t;

/* Pushed to a window's owner whenever the compositor changes the size of its
 * content area. The backing store is a shared-memory object sized to that
 * area, so a resize replaces it: the handle the client mapped is closed and a
 * new one takes its place. See window_poll_resize(). */
typedef struct {
    wm_msg_header_t header;
    uint32_t width;
    uint32_t height;
} wm_window_resized_t;

#define WM_SET_TITLE_MAX 128u
typedef struct {
    wm_msg_header_t header;
    char title[WM_SET_TITLE_MAX];
} wm_set_title_t;

typedef struct {
    wm_msg_header_t header;
    int32_t status;
    int32_t shared_memory_handle;
    uint32_t width;
    uint32_t height;
    uint32_t size_bytes;
} wm_backing_store_response_t;
