#include "flux.h"

#include <drm_fourcc.h>
#include <linux/input-event-codes.h>
#include <png.h>
#include <wlr/interfaces/wlr_buffer.h>

#define MIN_CLIENT_WIDTH 120
#define MIN_CLIENT_HEIGHT 80
#define FOOT_DRAG_HEIGHT 32
#define FOOT_DRAG_SIDE_PAD 6

static void raise_cursor_to_top(struct flux_server *server) {
	if (server->cursor_tree) {
		wlr_scene_node_raise_to_top(&server->cursor_tree->node);
	}
}

static void clamp_cursor_to_layout(struct flux_server *server) {
	struct wlr_box box = {0};
	wlr_output_layout_get_box(server->output_layout, NULL, &box);
	if (box.width <= 0 || box.height <= 0) {
		return;
	}

	double x = server->cursor->x;
	double y = server->cursor->y;
	double max_x = (double)box.x + (double)box.width - 1.0;
	double max_y = (double)box.y + (double)box.height - 1.0;
	if (x < box.x) {
		x = box.x;
	} else if (x > max_x) {
		x = max_x;
	}
	if (y < box.y) {
		y = box.y;
	} else if (y > max_y) {
		y = max_y;
	}

	if (x != server->cursor->x || y != server->cursor->y) {
		wlr_cursor_warp_closest(server->cursor, NULL, x, y);
	}
	server->cursor_x = server->cursor->x;
	server->cursor_y = server->cursor->y;
}

static void enable_drawn_cursor_fallback(struct flux_server *server, const char *reason) {
	if (!server->use_drawn_cursor) {
		wlr_log(WLR_ERROR, "theme cursor unavailable, falling back to drawn cursor (%s)",
			reason ? reason : "unknown");
	}

	server->use_drawn_cursor = true;
	wlr_cursor_unset_image(server->cursor);
	if (!server->cursor_tree) {
		create_cursor_pointer(server);
	}
}

static bool compositor_mod_down(struct flux_server *server) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (!keyboard) {
		return false;
	}

	uint32_t mods = wlr_keyboard_get_modifiers(keyboard);
	return (mods & server->keybind_mod_mask) != 0;
}

static bool point_in_foot_drag_region(struct flux_view *view, double lx, double ly) {
	if (!view || view->use_server_decorations ||
			!view->xdg_surface || !view->xdg_surface->toplevel) {
		return false;
	}

	const char *app_id = view->xdg_surface->toplevel->app_id;
	if (!app_id || strstr(app_id, "foot") == NULL) {
		return false;
	}

	double local_x = lx - view->x;
	double local_y = ly - view->y;
	if (local_x < FOOT_DRAG_SIDE_PAD || local_x >= view->width - FOOT_DRAG_SIDE_PAD) {
		return false;
	}
	return local_y >= 0 && local_y < FOOT_DRAG_HEIGHT;
}

static void begin_compositor_resize(struct flux_server *server,
		struct flux_view *view, uint32_t resize_edges) {
	if (!server || !view || resize_edges == WLR_EDGE_NONE) {
		return;
	}

	focus_view(view, view->xdg_surface->surface);
	server->cursor_mode = CURSOR_RESIZE;
	server->grabbed_view = view;
	server->interactive_grab_from_client = false;
	server->resize_edges = resize_edges;
	server->resize_init_x = view->x;
	server->resize_init_y = view->y;
	server->resize_init_width = view->width;
	server->resize_init_height = view->height;
	server->resize_cursor_start_x = server->cursor_x;
	server->resize_cursor_start_y = server->cursor_y;
	server->suppress_button_until_release = true;
}

static void begin_compositor_move(struct flux_server *server, struct flux_view *view) {
	if (!server || !view) {
		return;
	}

	focus_view(view, view->xdg_surface->surface);
	server->cursor_mode = CURSOR_MOVE;
	server->grabbed_view = view;
	server->interactive_grab_from_client = false;
	server->grab_x = server->cursor_x - view->x;
	server->grab_y = server->cursor_y - view->y;
	server->suppress_button_until_release = true;
}

void apply_default_cursor(struct flux_server *server) {
	static bool theme_probe_done = false;
	static bool theme_probe_ok = false;

	if (server->use_drawn_cursor) {
		if (!server->cursor_tree) {
			wlr_cursor_unset_image(server->cursor);
			create_cursor_pointer(server);
		}
		return;
	}

	if (!server->xcursor_manager) {
		enable_drawn_cursor_fallback(server, "xcursor manager missing");
		return;
	}

	if (!theme_probe_done) {
		theme_probe_done = true;
		if (!wlr_xcursor_manager_load(server->xcursor_manager, 1.0f)) {
			enable_drawn_cursor_fallback(server, "failed to load xcursor theme");
			return;
		}

		struct wlr_xcursor *arrow =
			wlr_xcursor_manager_get_xcursor(server->xcursor_manager, "left_ptr", 1.0f);
		theme_probe_ok = arrow != NULL;
		if (!theme_probe_ok) {
			enable_drawn_cursor_fallback(server, "left_ptr not found in xcursor theme");
			return;
		}
	}

	if (!theme_probe_ok) {
		enable_drawn_cursor_fallback(server, "cursor theme probe failed");
		return;
	}

	wlr_cursor_set_xcursor(server->cursor, server->xcursor_manager, "left_ptr");
}

void cursor_shape_request_set_shape_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(
		listener, server, cursor_shape_request_set_shape);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (event->seat_client != server->seat->pointer_state.focused_client) {
		return;
	}
	if (!wlr_seat_client_validate_event_serial(event->seat_client, event->serial)) {
		return;
	}

	if (server->use_drawn_cursor || !server->xcursor_manager) {
		return;
	}

	const char *shape_name = wlr_cursor_shape_v1_name(event->shape);
	if (shape_name) {
		struct wlr_xcursor *shape =
			wlr_xcursor_manager_get_xcursor(server->xcursor_manager, shape_name, 1.0f);
		if (!shape) {
			apply_default_cursor(server);
			return;
		}
		wlr_cursor_set_xcursor(server->cursor, server->xcursor_manager, shape_name);
		return;
	}

	apply_default_cursor(server);
}

void seat_request_set_cursor_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(listener, server, seat_request_set_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;

	if (event->seat_client != server->seat->pointer_state.focused_client) {
		return;
	}
	if (!wlr_seat_client_validate_event_serial(event->seat_client, event->serial)) {
		return;
	}

	if (server->use_drawn_cursor) {
		return;
	}

	if (event->surface) {
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
		return;
	}

	apply_default_cursor(server);
}

static void process_cursor_motion(struct flux_server *server, uint32_t time_msec) {
	clamp_cursor_to_layout(server);

	if (server->cursor_tree) {
		wlr_scene_node_set_position(&server->cursor_tree->node,
			(int)server->cursor_x - server->cursor_hotspot_x,
			(int)server->cursor_y - server->cursor_hotspot_y);
	}

	raise_cursor_to_top(server);

	if (server->cursor_mode == CURSOR_MOVE && server->grabbed_view) {
		int nx = (int)(server->cursor_x - server->grab_x);
		int ny = (int)(server->cursor_y - server->grab_y);
		server->grabbed_view->x = nx;
		server->grabbed_view->y = ny;
		wlr_scene_node_set_position(&server->grabbed_view->frame_tree->node, nx, ny);
		return;
	}

	if (server->cursor_mode == CURSOR_RESIZE && server->grabbed_view) {
		struct flux_view *view = server->grabbed_view;
		int border = view->use_server_decorations ? BORDER_PX : 0;
		int title_h = view->use_server_decorations ? TITLEBAR_PX : 0;
		int dx = (int)lround(server->cursor_x - server->resize_cursor_start_x);
		int dy = (int)lround(server->cursor_y - server->resize_cursor_start_y);

		int nx = server->resize_init_x;
		int ny = server->resize_init_y;
		int nw = server->resize_init_width;
		int nh = server->resize_init_height;

		if (server->resize_edges & WLR_EDGE_LEFT) {
			nx = server->resize_init_x + dx;
			nw = server->resize_init_width - dx;
		}
		if (server->resize_edges & WLR_EDGE_RIGHT) {
			nw = server->resize_init_width + dx;
		}
		if (server->resize_edges & WLR_EDGE_TOP) {
			ny = server->resize_init_y + dy;
			nh = server->resize_init_height - dy;
		}
		if (server->resize_edges & WLR_EDGE_BOTTOM) {
			nh = server->resize_init_height + dy;
		}

		int min_w = border * 2 + MIN_CLIENT_WIDTH;
		int min_h = title_h + border + MIN_CLIENT_HEIGHT;
		if (nw < min_w) {
			if (server->resize_edges & WLR_EDGE_LEFT) {
				nx += nw - min_w;
			}
			nw = min_w;
		}
		if (nh < min_h) {
			if (server->resize_edges & WLR_EDGE_TOP) {
				ny += nh - min_h;
			}
			nh = min_h;
		}

		view->x = nx;
		view->y = ny;
		wlr_scene_node_set_position(&view->frame_tree->node, nx, ny);
		view_set_frame_size(view, nw, nh);

		int surface_w = nw - border * 2;
		int surface_h = nh - title_h - border;
		if (surface_w < 1) {
			surface_w = 1;
		}
		if (surface_h < 1) {
			surface_h = 1;
		}
		wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, surface_w, surface_h);
		wlr_xdg_surface_schedule_configure(view->xdg_surface);
		return;
	}

	struct wlr_surface *surface = NULL;
	double sx = 0.0, sy = 0.0;
	struct flux_view *view = view_at(server, server->cursor_x, server->cursor_y,
		&surface, &sx, &sy);

	if (!surface) {
		wlr_seat_pointer_clear_focus(server->seat);
		if (!server->use_drawn_cursor) {
			apply_default_cursor(server);
		}
		return;
	}

	focus_view(view, surface);
	wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
}

void cursor_motion_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x, event->delta_y);
	server->cursor_x = server->cursor->x;
	server->cursor_y = server->cursor->y;
	process_cursor_motion(server, event->time_msec);
}

void cursor_motion_absolute_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;

	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	server->cursor_x = server->cursor->x;
	server->cursor_y = server->cursor->y;
	process_cursor_motion(server, event->time_msec);
}

void cursor_button_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	// Make sure pointer focus is up-to-date even when the user clicks without moving.
	process_cursor_motion(server, event->time_msec);

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		struct flux_view *taskbar_view =
			taskbar_view_at(server, server->cursor_x, server->cursor_y);
		if (taskbar_view) {
			server->pressed_taskbar_view = taskbar_view;
			server->suppress_button_until_release = true;
			taskbar_mark_dirty(server);
			return;
		}
	}

	struct wlr_surface *surface = NULL;
	double sx = 0.0, sy = 0.0;
	struct flux_view *view = view_at(server, server->cursor_x, server->cursor_y,
		&surface, &sx, &sy);
	struct flux_view *frame_view = view ? view :
		view_frame_at(server, server->cursor_x, server->cursor_y);
	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
			event->button == BTN_LEFT && frame_view) {
		uint32_t edge_resize = view_resize_edges_at(frame_view,
			server->cursor_x, server->cursor_y);
		if (edge_resize != WLR_EDGE_NONE) {
			begin_compositor_resize(server, frame_view, edge_resize);
			return;
		}

		/* Move only from explicit outer border ring, not interior holes. */
		if (!surface &&
				view_point_in_frame_border(frame_view, server->cursor_x, server->cursor_y)) {
			begin_compositor_move(server, frame_view);
			return;
		}
	}
	if (!view && !server->suppress_button_until_release) {
		wlr_seat_pointer_notify_button(server->seat, event->time_msec,
			event->button, event->state);
		return;
	}

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		if (server->pressed_taskbar_view) {
			struct flux_view *pressed = server->pressed_taskbar_view;
			server->pressed_taskbar_view = NULL;
			struct flux_view *taskbar_view =
				taskbar_view_at(server, server->cursor_x, server->cursor_y);
			if (taskbar_view == pressed && pressed->mapped && pressed->minimized) {
				view_begin_restore_animation(pressed, event->time_msec);
			}
			taskbar_mark_dirty(server);
			server->suppress_button_until_release = false;
			return;
		}

		if (server->cursor_mode == CURSOR_MOVE ||
				server->cursor_mode == CURSOR_RESIZE ||
				server->suppress_button_until_release) {
			if (server->interactive_grab_from_client && server->grabbed_view) {
				wlr_seat_pointer_notify_button(server->seat, event->time_msec,
					event->button, event->state);
			}
			server->cursor_mode = CURSOR_PASSTHROUGH;
			server->grabbed_view = NULL;
			server->resize_edges = WLR_EDGE_NONE;
			server->suppress_button_until_release = false;
			server->interactive_grab_from_client = false;
			return;
		}

		wlr_seat_pointer_notify_button(server->seat, event->time_msec,
			event->button, event->state);
		return;
	}

	if (!view) {
		return;
	}

	if (event->button == BTN_LEFT && compositor_mod_down(server)) {
		begin_compositor_move(server, view);
		return;
	}

	if (event->button == BTN_LEFT &&
			point_in_foot_drag_region(view, server->cursor_x, server->cursor_y)) {
		begin_compositor_move(server, view);
		return;
	}

	if (point_in_minimize_button(view, server->cursor_x, server->cursor_y)) {
		server->suppress_button_until_release = true;
		view_begin_minimize_animation(view, event->time_msec);
		return;
	}

	uint32_t resize_edges = view_resize_edges_at(view, server->cursor_x, server->cursor_y);
	if (resize_edges != WLR_EDGE_NONE) {
		begin_compositor_resize(server, view, resize_edges);
		return;
	}

	if (point_in_titlebar_drag_region(view, server->cursor_x, server->cursor_y)) {
		begin_compositor_move(server, view);
		return;
	}

	if (surface) {
		focus_view(view, surface);
		wlr_seat_pointer_notify_button(server->seat, event->time_msec,
			event->button, event->state);
		return;
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
		event->button, event->state);
}

void cursor_axis_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
		event->orientation, event->delta, event->delta_discrete,
		event->source, event->relative_direction);
}

void cursor_frame_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

struct flux_cursor_file_buffer {
	struct wlr_buffer base;
	uint32_t *data;
	size_t stride;
};

static struct flux_cursor_file_buffer *cursor_file_buffer_from_base(
		struct wlr_buffer *buffer) {
	struct flux_cursor_file_buffer *cursor_buffer =
		wl_container_of(buffer, cursor_buffer, base);
	return cursor_buffer;
}

static void cursor_file_buffer_destroy(struct wlr_buffer *buffer) {
	struct flux_cursor_file_buffer *cursor_buffer =
		cursor_file_buffer_from_base(buffer);
	free(cursor_buffer->data);
	free(cursor_buffer);
}

static bool cursor_file_buffer_begin_data_ptr_access(struct wlr_buffer *buffer,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	(void)flags;
	struct flux_cursor_file_buffer *cursor_buffer =
		cursor_file_buffer_from_base(buffer);
	*data = cursor_buffer->data;
	*format = DRM_FORMAT_ARGB8888;
	*stride = cursor_buffer->stride;
	return true;
}

static void cursor_file_buffer_end_data_ptr_access(struct wlr_buffer *buffer) {
	(void)buffer;
}

static const struct wlr_buffer_impl cursor_file_buffer_impl = {
	.destroy = cursor_file_buffer_destroy,
	.begin_data_ptr_access = cursor_file_buffer_begin_data_ptr_access,
	.end_data_ptr_access = cursor_file_buffer_end_data_ptr_access,
};

static struct flux_cursor_file_buffer *cursor_file_buffer_create(int width, int height) {
	if (width <= 0 || height <= 0) {
		return NULL;
	}

	struct flux_cursor_file_buffer *cursor_buffer = calloc(1, sizeof(*cursor_buffer));
	if (!cursor_buffer) {
		return NULL;
	}

	wlr_buffer_init(&cursor_buffer->base, &cursor_file_buffer_impl, width, height);
	cursor_buffer->stride = (size_t)width * 4;
	cursor_buffer->data = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
	if (!cursor_buffer->data) {
		wlr_buffer_drop(&cursor_buffer->base);
		return NULL;
	}

	return cursor_buffer;
}

static bool resolve_cursor_image_path(char out[PATH_MAX]) {
	const char *override = getenv("FLUX_CURSOR_IMAGE_PATH");
	if (override && override[0] != '\0' && access(override, R_OK) == 0) {
		int n = snprintf(out, PATH_MAX, "%s", override);
		if (n > 0 && n < PATH_MAX) {
			return true;
		}
	}

	const char *home = getenv("HOME");
	if (home && home[0] != '\0') {
		int n = snprintf(out, PATH_MAX, "%s/flux/mouse/mouse.png", home);
		if (n > 0 && n < PATH_MAX && access(out, R_OK) == 0) {
			return true;
		}
	}

	if (access("mouse/mouse.png", R_OK) == 0) {
		snprintf(out, PATH_MAX, "%s", "mouse/mouse.png");
		return true;
	}

	static const char *system_paths[] = {
		"/usr/local/share/flux/mouse/mouse.png",
		"/usr/share/flux/mouse/mouse.png",
	};
	for (size_t i = 0; i < sizeof(system_paths) / sizeof(system_paths[0]); i++) {
		if (access(system_paths[i], R_OK) != 0) {
			continue;
		}
		int n = snprintf(out, PATH_MAX, "%s", system_paths[i]);
		if (n > 0 && n < PATH_MAX) {
			return true;
		}
	}

	return false;
}

static struct flux_cursor_file_buffer *load_cursor_png_buffer(const char *path,
		int *hotspot_x, int *hotspot_y) {
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		return NULL;
	}

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		fclose(fp);
		return NULL;
	}

	png_infop info = png_create_info_struct(png);
	if (!info) {
		png_destroy_read_struct(&png, NULL, NULL);
		fclose(fp);
		return NULL;
	}

	png_bytep src = NULL;
	png_bytep *rows = NULL;
	struct flux_cursor_file_buffer *cursor_buffer = NULL;

	if (setjmp(png_jmpbuf(png))) {
		cursor_buffer = NULL;
		goto done;
	}

	png_init_io(png, fp);
	png_read_info(png, info);

	png_uint_32 src_w = 0, src_h = 0;
	int bit_depth = 0, color_type = 0;
	png_get_IHDR(png, info, &src_w, &src_h, &bit_depth, &color_type, NULL, NULL, NULL);

	if (bit_depth == 16) {
		png_set_strip_16(png);
	}
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(png);
	}
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
		png_set_expand_gray_1_2_4_to_8(png);
	}
	if (png_get_valid(png, info, PNG_INFO_tRNS)) {
		png_set_tRNS_to_alpha(png);
	}
	if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
			color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	}
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
		png_set_gray_to_rgb(png);
	}

	png_read_update_info(png, info);
	size_t src_stride = png_get_rowbytes(png, info);
	src = calloc((size_t)src_h, src_stride);
	rows = calloc((size_t)src_h, sizeof(*rows));
	if (!src || !rows) {
		goto done;
	}

	for (png_uint_32 y = 0; y < src_h; y++) {
		rows[y] = src + (size_t)y * src_stride;
	}
	png_read_image(png, rows);

	int min_x = (int)src_w;
	int min_y = (int)src_h;
	int max_x = -1;
	int max_y = -1;

	for (png_uint_32 y = 0; y < src_h; y++) {
		png_bytep row = rows[y];
		for (png_uint_32 x = 0; x < src_w; x++) {
			uint8_t a = row[x * 4 + 3];
			if (a == 0) {
				continue;
			}

			if ((int)x < min_x) {
				min_x = (int)x;
			}
			if ((int)y < min_y) {
				min_y = (int)y;
			}
			if ((int)x > max_x) {
				max_x = (int)x;
			}
			if ((int)y > max_y) {
				max_y = (int)y;
			}
		}
	}

	if (max_x < min_x || max_y < min_y) {
		goto done;
	}

	int crop_w = max_x - min_x + 1;
	int crop_h = max_y - min_y + 1;
	int tip_x = -1;
	int tip_y = -1;
	/*
	 * Detect directional tip first (left/right-pointing cursor art), then
	 * fall back to nearest top-left visible pixel.
	 */
	int left_density = 0;
	int right_density = 0;
	double center_y_sum = 0.0;
	int center_count = 0;
	for (int y = min_y; y <= max_y; y++) {
		png_bytep row = rows[y];
		for (int x = min_x; x <= max_x; x++) {
			uint8_t a = row[x * 4 + 3];
			if (a < 32) {
				continue;
			}

			int cx = x - min_x;
			int cy = y - min_y;
			if ((float)cx < (float)crop_w * 0.25f) {
				left_density++;
			}
			if ((float)cx >= (float)crop_w * 0.75f) {
				right_density++;
			}
			center_y_sum += (double)cy;
			center_count++;
		}
	}

	double center_y = center_count > 0 ? center_y_sum / (double)center_count :
		(double)(crop_h - 1) * 0.5;

	bool likely_tip_right = right_density * 5 < left_density * 3;
	bool likely_tip_left = left_density * 5 < right_density * 3;
	if (likely_tip_right || likely_tip_left) {
		int start_x = likely_tip_right ? max_x : min_x;
		int end_x = likely_tip_right ? min_x : max_x;
		int step_x = likely_tip_right ? -1 : 1;

		for (int x = start_x; likely_tip_right ? (x >= end_x) : (x <= end_x); x += step_x) {
			int best_y_at_x = -1;
			int best_alpha = -1;
			double best_center_dist = 0.0;
			for (int y = min_y; y <= max_y; y++) {
				png_bytep row = rows[y];
				uint8_t a = row[x * 4 + 3];
				if (a < 32) {
					continue;
				}

				double cy = (double)(y - min_y);
				double center_dist = fabs(cy - center_y);
				if (a > best_alpha ||
						(a == best_alpha && (best_y_at_x < 0 || center_dist < best_center_dist))) {
					best_alpha = a;
					best_y_at_x = y;
					best_center_dist = center_dist;
				}
			}

			if (best_y_at_x >= 0) {
				tip_x = x - min_x;
				tip_y = best_y_at_x - min_y;
				break;
			}
		}
	}

	const uint8_t thresholds[] = {8, 1};
	for (size_t pass = 0; pass < sizeof(thresholds) / sizeof(thresholds[0]); pass++) {
		if (tip_x >= 0 && tip_y >= 0) {
			break;
		}
		uint8_t threshold = thresholds[pass];
		int best_dist2 = INT_MAX;
		int pass_tip_x = -1;
		int pass_tip_y = -1;

		for (int y = min_y; y <= max_y; y++) {
			png_bytep row = rows[y];
			for (int x = min_x; x <= max_x; x++) {
				uint8_t a = row[x * 4 + 3];
				if (a < threshold) {
					continue;
				}

				int cx = x - min_x;
				int cy = y - min_y;
				int d2 = cx * cx + cy * cy;
				if (d2 < best_dist2 ||
						(d2 == best_dist2 &&
						(cy < pass_tip_y || (cy == pass_tip_y && cx < pass_tip_x)))) {
					best_dist2 = d2;
					pass_tip_x = cx;
					pass_tip_y = cy;
				}
			}
		}

		if (pass_tip_x >= 0 && pass_tip_y >= 0) {
			tip_x = pass_tip_x;
			tip_y = pass_tip_y;
			break;
		}
	}

	if (tip_x < 0 || tip_y < 0) {
		tip_x = 0;
		tip_y = 0;
	}

	cursor_buffer = cursor_file_buffer_create(crop_w, crop_h);
	if (!cursor_buffer) {
		goto done;
	}

	for (int y = 0; y < crop_h; y++) {
		png_bytep row = rows[(size_t)min_y + (size_t)y];
		uint32_t *dst_row = cursor_buffer->data + (size_t)y * (size_t)crop_w;
		for (int x = 0; x < crop_w; x++) {
			png_bytep px = row + ((size_t)min_x + (size_t)x) * 4;
			uint8_t r = px[0];
			uint8_t g = px[1];
			uint8_t b = px[2];
			uint8_t a = px[3];

			// wl_shm ARGB requires pre-multiplied alpha.
			uint8_t pr = (uint8_t)((r * a + 127) / 255);
			uint8_t pg = (uint8_t)((g * a + 127) / 255);
			uint8_t pb = (uint8_t)((b * a + 127) / 255);
			dst_row[x] = ((uint32_t)a << 24) | ((uint32_t)pr << 16) |
				((uint32_t)pg << 8) | (uint32_t)pb;
		}
	}

	*hotspot_x = tip_x;
	*hotspot_y = tip_y;
	wlr_log(WLR_INFO, "loaded cursor image %s (%dx%d hotspot=%d,%d)",
		path, crop_w, crop_h, *hotspot_x, *hotspot_y);

done:
	free(rows);
	free(src);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);
	return cursor_buffer;
}

struct cursor_segment {
	int x;
	int y;
	int w;
};

static float cursor_draw_scale(struct flux_server *server) {
	const char *env = getenv("FLUX_CURSOR_DRAW_SCALE");
	if (env && env[0] != '\0') {
		char *end = NULL;
		float value = strtof(env, &end);
		if (end && end != env && isfinite(value) && value > 0.0f) {
			if (value < 0.25f) {
				value = 0.25f;
			} else if (value > 4.0f) {
				value = 4.0f;
			}
			return value;
		}
	}

	float max_output_scale = 1.0f;
	struct flux_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (!output->wlr_output) {
			continue;
		}
		if (output->wlr_output->scale > max_output_scale) {
			max_output_scale = output->wlr_output->scale;
		}
	}

	/* Keep cursor roughly physical-size stable on HiDPI outputs. */
	if (max_output_scale > 1.0f) {
		return 1.0f / max_output_scale;
	}
	return 1.0f;
}

static void draw_cursor_segments(struct wlr_scene_tree *tree,
		const struct cursor_segment *segments, size_t count,
		const float color[4], float scale) {
	for (size_t i = 0; i < count; i++) {
		if (segments[i].w <= 0) {
			continue;
		}

		int x = (int)lroundf((float)segments[i].x * scale);
		int y = (int)lroundf((float)segments[i].y * scale);
		int w = (int)lroundf((float)segments[i].w * scale);
		if (w < 1) {
			w = 1;
		}

		struct wlr_scene_rect *r =
			wlr_scene_rect_create(tree, w, 1, color);
		wlr_scene_node_set_position(&r->node, x, y);
	}
}

void create_cursor_pointer(struct flux_server *server) {
	server->cursor_tree = wlr_scene_tree_create(&server->scene->tree);
	wlr_scene_node_set_position(&server->cursor_tree->node, 0, 0);
	wlr_scene_node_raise_to_top(&server->cursor_tree->node);
	float draw_scale = cursor_draw_scale(server);

	bool use_image_cursor = env_int("FLUX_CURSOR_IMAGE", 1) != 0;
	if (use_image_cursor) {
		char cursor_path[PATH_MAX];
		if (resolve_cursor_image_path(cursor_path)) {
			int hotspot_x = 0;
			int hotspot_y = 0;
			struct flux_cursor_file_buffer *cursor_buffer =
				load_cursor_png_buffer(cursor_path, &hotspot_x, &hotspot_y);
			if (cursor_buffer) {
				struct wlr_scene_buffer *scene_buffer =
					wlr_scene_buffer_create(server->cursor_tree, &cursor_buffer->base);
				if (scene_buffer) {
					int src_w = cursor_buffer->base.width;
					int src_h = cursor_buffer->base.height;
					int dst_w = (int)lroundf((float)src_w * draw_scale);
					int dst_h = (int)lroundf((float)src_h * draw_scale);
					if (dst_w < 1) {
						dst_w = 1;
					}
					if (dst_h < 1) {
						dst_h = 1;
					}

					if (dst_w != src_w || dst_h != src_h) {
						wlr_scene_buffer_set_dest_size(scene_buffer, dst_w, dst_h);
						wlr_log(WLR_INFO,
							"image cursor scale=%.2f src=%dx%d dst=%dx%d",
							draw_scale, src_w, src_h, dst_w, dst_h);
					}

					float sx = (float)dst_w / (float)src_w;
					float sy = (float)dst_h / (float)src_h;
					/*
					 * Always use detected hotspot for image cursors. This avoids
					 * stale env overrides from shifting click targets far away.
					 */
					server->cursor_hotspot_x = (int)lroundf((float)hotspot_x * sx);
					server->cursor_hotspot_y = (int)lroundf((float)hotspot_y * sy);
					wlr_log(WLR_INFO, "using image cursor hotspot=%d,%d",
						server->cursor_hotspot_x, server->cursor_hotspot_y);
					wlr_buffer_drop(&cursor_buffer->base);
					return;
				}
				wlr_log(WLR_ERROR, "failed to create scene buffer for cursor image %s",
					cursor_path);
				wlr_buffer_drop(&cursor_buffer->base);
			} else {
				wlr_log(WLR_ERROR, "failed to decode cursor image %s; using drawn pointer",
					cursor_path);
			}
		} else {
			wlr_log(WLR_INFO,
				"FLUX_CURSOR_IMAGE=1 but mouse/mouse.png was not found; using drawn pointer");
		}
	}

	/*
	 * Pixel cursor styled after mouse/mouse.png:
	 * white body + slate wedge + dark stem, with crisp black outline.
	 * Hotspot remains at (0,0).
	 */
	static const struct cursor_segment outline[] = {
		{0, 0, 2},  {0, 1, 3},  {0, 2, 4},  {0, 3, 6},
		{0, 4, 8},  {0, 5, 10}, {0, 6, 12}, {0, 7, 14},
		{0, 8, 16}, {0, 9, 18}, {0, 10, 20}, {0, 11, 22},
		{0, 12, 24}, {0, 13, 23}, {0, 14, 21}, {0, 15, 19},
		{0, 16, 16}, {0, 17, 10}, {12, 17, 5}, {0, 18, 9},
		{12, 18, 4}, {0, 19, 8}, {12, 19, 3}, {0, 20, 7},
		{12, 20, 2}, {0, 21, 6}, {11, 21, 2}, {0, 22, 5},
		{10, 22, 2}, {0, 23, 4}, {9, 23, 2},
	};
	static const struct cursor_segment white[] = {
		{1, 1, 1},  {1, 2, 2},  {1, 3, 4},  {1, 4, 6},
		{1, 5, 8},  {1, 6, 10}, {1, 7, 12}, {1, 8, 14},
		{1, 9, 16}, {1, 10, 18}, {1, 11, 20}, {1, 12, 21},
		{1, 13, 20}, {1, 14, 18}, {1, 15, 16}, {1, 16, 11},
		{1, 17, 8}, {1, 18, 7}, {1, 19, 6}, {1, 20, 5},
		{1, 21, 4}, {1, 22, 3},
	};
	static const struct cursor_segment shadow[] = {
		{4, 4, 2},  {5, 5, 3},  {6, 6, 4},  {7, 7, 5},
		{8, 8, 6},  {9, 9, 7},  {10, 10, 8}, {11, 11, 9},
		{12, 12, 9}, {13, 13, 8}, {13, 14, 7}, {12, 15, 6},
		{12, 16, 4},
	};
	static const struct cursor_segment stem[] = {
		{12, 17, 4}, {12, 18, 4}, {12, 19, 3}, {12, 20, 2},
		{11, 21, 2}, {10, 22, 2},
	};

	static const float cursor_shadow[4] = {0.53f, 0.57f, 0.67f, 1.0f};
	static const float cursor_stem[4] = {0.22f, 0.24f, 0.31f, 1.0f};
	if (draw_scale != 1.0f) {
		wlr_log(WLR_INFO, "drawn cursor scale=%.2f", draw_scale);
	}

	draw_cursor_segments(server->cursor_tree,
		outline, sizeof(outline) / sizeof(outline[0]), COLOR_CURSOR_BLACK, draw_scale);
	draw_cursor_segments(server->cursor_tree,
		white, sizeof(white) / sizeof(white[0]), COLOR_CURSOR_WHITE, draw_scale);
	draw_cursor_segments(server->cursor_tree,
		shadow, sizeof(shadow) / sizeof(shadow[0]), cursor_shadow, draw_scale);
	draw_cursor_segments(server->cursor_tree,
		stem, sizeof(stem) / sizeof(stem[0]), cursor_stem, draw_scale);
}
