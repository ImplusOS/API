#pragma once

#include <stdint.h>
#include <stdbool.h>


#define WM_CREATE_WINDOW        1
#define WM_DESTROY_WINDOW       2
#define WM_SET_WINDOW_RECT      3
#define WM_SET_WINDOW_ICON      9
#define WM_SHOW_WINDOW          4
#define WM_HIDE_WINDOW          5
#define WM_RAISE_WINDOW         6
#define WM_LOWER_WINDOW         7
#define WM_SET_FOCUS            8
#define WM_DRAW_PIXEL          10
#define WM_DRAW_RECT           11
#define WM_CLEAR_WINDOW        12
#define WM_UPDATE_COMPLETE     13
#define WM_BLIT_BUFFER         14
#define WM_SET_WINDOW_SYSTEM   15
#define WM_DRAW_TEXT           16

#define WM_KEYBOARD_EVENT      20
#define WM_MOUSE_EVENT         21

#define WM_SUBSCRIBE_INPUT     30
#define WM_UNSUBSCRIBE_INPUT   31

#define WM_GET_WINDOW_RECT     40
#define WM_WINDOW_CREATED      41
#define WM_WINDOW_DESTROYED    42
#define WM_WINDOW_MOVED        43

#define WM_REGISTER_SERVICE    50
#define WM_GET_DISPLAY_INFO    51
#define WM_GET_FOCUS           52

#define WM_SET_LAYOUT_XML_START 60
#define WM_SET_LAYOUT_XML_CHUNK 61
#define WM_SET_LAYOUT_XML_END   62
#define WM_UPDATE_CLOCK         70
#define WM_SHOW_NOTIFICATION    80
#define WM_SHOW_DIALOG          81

#define WM_SET_THEME            90
#define WM_RELOAD_BACKGROUND    91
#define WM_GET_CAPABILITIES     92

#define WM_GET_BACKING_STORE   100
#define WM_BACKING_STORE_READY 101
#define WM_DAMAGE              102
#define WM_BEGIN_TRANSACTION   103
#define WM_END_TRANSACTION     104
#define WM_SET_CURSOR          110
#define WM_SET_WINDOW_ICON_PATH 111
#define WM_SET_WINDOW_SURFACE_OPAQUE 112


#define WM_STATUS_OK             0
#define WM_STATUS_INVALID_ARG  (-22)
#define WM_STATUS_NOT_FOUND    (-2)
#define WM_STATUS_DENIED       (-13)
#define WM_STATUS_TOO_LARGE    (-7)
#define WM_STATUS_UNSUPPORTED  (-95)

#define WM_CAP_SERVER_SURFACE   (1u << 0)
#define WM_CAP_DAMAGE_REGIONS   (1u << 1)
#define WM_CAP_TRANSACTIONS     (1u << 2)
#define WM_CAP_THEME_ENGINE     (1u << 3)
#define WM_CAP_NOTIFICATIONS    (1u << 4)
#define WM_CAP_SHARED_SURFACE   (1u << 5)
#define WM_CAP_DIALOGS          (1u << 6)


typedef struct {
    uint32_t type;
    uint32_t request_id;
    uint32_t window_id;
} wm_msg_header_t;

typedef struct {
    wm_msg_header_t header;
    int32_t status;
    int32_t shared_memory_handle;
    uint32_t width;
    uint32_t height;
    uint32_t size_bytes;
} wm_backing_store_response_t;
