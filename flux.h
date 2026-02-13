#ifndef FLUX_H
#define FLUX_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#define BORDER_PX 2
#define TITLEBAR_PX 28
#define BTN_W 18
#define BTN_H 14
#define BTN_PAD 6

extern const float COLOR_TITLE_ACTIVE[4];
extern const float COLOR_TITLE_INACTIVE[4];
extern const float COLOR_BORDER[4];
extern const float COLOR_MIN_BUTTON[4];
extern const float COLOR_BACKGROUND[4];
extern const float COLOR_TASKBAR_BG[4];
extern const float COLOR_TASKBAR_BUTTON[4];
extern const float COLOR_TASKBAR_TEXT[4];
extern const float COLOR_CURSOR_BLACK[4];
extern const float COLOR_CURSOR_WHITE[4];

struct flux_server;
struct flux_view;

struct flux_output {
	struct wl_list link;
	struct flux_server *server;
	struct wlr_output *wlr_output;
	struct wlr_scene_rect *background_rect;
	struct wl_listener frame;
	struct wl_listener destroy;
};

struct flux_keyboard {
	struct wl_list link;
	struct flux_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct flux_view {
	struct wl_list link;
	struct flux_server *server;
	struct wlr_xdg_surface *xdg_surface;
	struct wlr_xdg_toplevel_decoration_v1 *xdg_decoration;

	bool mapped;
	bool minimized;
	bool minimizing_animation;
	uint32_t minimize_animation_start_msec;
	bool restoring_animation;
	uint32_t restore_animation_start_msec;
	double anim_from_cx;
	double anim_from_cy;
	double anim_to_cx;
	double anim_to_cy;
	float anim_from_scale;
	float anim_to_scale;
	float anim_from_alpha;
	float anim_to_alpha;
	int x;
	int y;
	int width;
	int height;
	int xdg_geo_x;
	int xdg_geo_y;
	int xdg_geo_width;
	int xdg_geo_height;
	int content_x;
	int content_y;
	bool use_server_decorations;
	int taskbar_x;
	int taskbar_y;
	int taskbar_width;
	int taskbar_height;
	bool taskbar_visible;

	struct wlr_scene_tree *frame_tree;
	struct wlr_scene_tree *content_tree;
	struct wlr_scene_rect *title_rect;
	struct wlr_scene_rect *left_border_rect;
	struct wlr_scene_rect *right_border_rect;
	struct wlr_scene_rect *bottom_border_rect;
	struct wlr_scene_rect *minimize_rect;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener request_move;
	struct wl_listener request_resize;
};

enum flux_cursor_mode {
	CURSOR_PASSTHROUGH,
	CURSOR_MOVE,
	CURSOR_RESIZE,
};

struct flux_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_output_layout *output_layout;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_subcompositor *subcompositor;
	struct wlr_primary_selection_v1_device_manager *primary_selection_v1;
	struct wlr_xdg_activation_v1 *xdg_activation_v1;
	struct wlr_viewporter *viewporter;
	struct wlr_fractional_scale_manager_v1 *fractional_scale_v1;
	struct wlr_cursor_shape_manager_v1 *cursor_shape_v1;
	struct wlr_xcursor_manager *xcursor_manager;
	struct wlr_text_input_manager_v3 *text_input_v3;
	struct wlr_input_method_manager_v2 *input_method_v2;
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_v1;
	struct wlr_scene_tree *taskbar_tree;
	struct wlr_scene_rect *taskbar_bg_rect;
	struct wlr_scene_tree *taskbar_buttons_tree;
	struct wlr_seat *seat;
	struct wlr_cursor *cursor;

	struct wl_list outputs;   // flux_output::link
	struct wl_list keyboards; // flux_keyboard::link
	struct wl_list views;     // flux_view::link (head = topmost)

	double cursor_x;
	double cursor_y;
	int cursor_hotspot_x;
	int cursor_hotspot_y;
	uint32_t keybind_mod_mask;
	enum flux_cursor_mode cursor_mode;
	struct flux_view *grabbed_view;
	struct flux_view *pressed_taskbar_view;
	uint32_t resize_edges;
	int resize_init_x;
	int resize_init_y;
	int resize_init_width;
	int resize_init_height;
	double resize_cursor_start_x;
	double resize_cursor_start_y;
	double grab_x;
	double grab_y;

	struct wlr_scene_tree *cursor_tree;

	struct wl_listener new_output;
	struct wl_listener new_input;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wl_listener seat_request_set_cursor;
	struct wl_listener cursor_shape_request_set_shape;
	struct wl_listener xdg_activation_request_activate;
	struct wl_listener xdg_decoration_new_toplevel;

	struct wl_event_source *sigint_source;
	struct wl_event_source *sigterm_source;

	bool suppress_button_until_release;
	bool interactive_grab_from_client;
	int next_view_x;
	int next_view_y;
	int taskbar_layout_x;
	int taskbar_layout_y;
	int taskbar_layout_width;
	int taskbar_layout_height;
	bool taskbar_dirty;
	bool use_drawn_cursor;
};

/* theme.c */

/* logging.c */
void init_logging(void);
void close_logging(void);
const char *flux_log_path(void);
void flux_log_callback(enum wlr_log_importance importance, const char *fmt, va_list args);
int handle_terminate_signal(int signal_number, void *data);
void setup_child_reaping(void);

/* config.c */
int env_int(const char *name, int fallback);
uint32_t parse_keybind_mod_mask(void);

/* launch.c */
const char *default_launch_command(void);
void launch_app(struct flux_server *server, const char *command);

/* view.c */
void place_new_view(struct flux_server *server, struct flux_view *view);
void configure_new_toplevel(struct flux_server *server, struct wlr_xdg_surface *xdg_surface);
struct flux_view *view_from_surface(struct flux_server *server, struct wlr_surface *surface);
void view_update_geometry(struct flux_view *view);
void view_set_frame_size(struct flux_view *view, int frame_width, int frame_height);
void view_set_server_decorations(struct flux_view *view, bool enabled);
void view_set_visible(struct flux_view *view, bool visible);
void view_begin_minimize_animation(struct flux_view *view, uint32_t time_msec);
void view_begin_restore_animation(struct flux_view *view, uint32_t time_msec);
bool view_tick_animations(struct flux_server *server, uint32_t time_msec);
void focus_view(struct flux_view *view, struct wlr_surface *surface);
struct flux_view *view_at(struct flux_server *server, double lx, double ly,
	struct wlr_surface **surface, double *sx, double *sy);
struct flux_view *view_frame_at(struct flux_server *server, double lx, double ly);
uint32_t view_resize_edges_at(struct flux_view *view, double lx, double ly);
bool view_point_in_frame_border(struct flux_view *view, double lx, double ly);
bool point_in_minimize_button(struct flux_view *view, double lx, double ly);
bool point_in_titlebar_drag_region(struct flux_view *view, double lx, double ly);

/* cursor.c */
void apply_default_cursor(struct flux_server *server);
void cursor_shape_request_set_shape_notify(struct wl_listener *listener, void *data);
void seat_request_set_cursor_notify(struct wl_listener *listener, void *data);
void cursor_motion_notify(struct wl_listener *listener, void *data);
void cursor_motion_absolute_notify(struct wl_listener *listener, void *data);
void cursor_button_notify(struct wl_listener *listener, void *data);
void cursor_axis_notify(struct wl_listener *listener, void *data);
void cursor_frame_notify(struct wl_listener *listener, void *data);
void create_cursor_pointer(struct flux_server *server);

/* output.c */
void new_output_notify(struct wl_listener *listener, void *data);

/* input.c */
void new_input_notify(struct wl_listener *listener, void *data);

/* xdg.c */
void xdg_activation_request_activate_notify(struct wl_listener *listener, void *data);
void xdg_decoration_new_toplevel_notify(struct wl_listener *listener, void *data);
void new_xdg_toplevel_notify(struct wl_listener *listener, void *data);

/* taskbar.c */
void taskbar_init(struct flux_server *server);
void taskbar_mark_dirty(struct flux_server *server);
void taskbar_update(struct flux_server *server);
struct flux_view *taskbar_view_at(struct flux_server *server, double lx, double ly);
bool taskbar_predict_button_box(struct flux_server *server, struct flux_view *target,
	bool include_target_if_not_minimized, struct wlr_box *out);

#endif
