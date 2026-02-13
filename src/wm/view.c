#include "flux.h"

#define MINIMIZE_ANIMATION_DURATION_MS 180
#define RESTORE_ANIMATION_DURATION_MS 180
#define MINIMIZE_ANIMATION_MIN_SCALE 0.12f

static int view_border_px(const struct flux_view *view) {
	return view->use_server_decorations ? BORDER_PX : 0;
}

static int view_titlebar_px(const struct flux_view *view) {
	return view->use_server_decorations ? TITLEBAR_PX : 0;
}

static bool view_app_id_contains(const struct flux_view *view, const char *needle) {
	if (!view || !needle || !view->xdg_surface || !view->xdg_surface->toplevel) {
		return false;
	}
	const char *app_id = view->xdg_surface->toplevel->app_id;
	return app_id && strstr(app_id, needle) != NULL;
}

static int clamp_hit_margin(const struct flux_view *view, int margin) {
	if (!view) {
		return 1;
	}

	int half_w = view->width / 2;
	int half_h = view->height / 2;
	if (margin > half_w) {
		margin = half_w;
	}
	if (margin > half_h) {
		margin = half_h;
	}
	if (margin < 1) {
		margin = 1;
	}
	return margin;
}

static int view_resize_hit_margin(const struct flux_view *view) {
	int margin = 0;
	if (view && !view->use_server_decorations) {
		/*
		 * CSD apps (Firefox/Thunar/etc.) need a practical compositor resize zone.
		 * Keep foot a bit larger due its dense top chrome.
		 */
		margin = view_app_id_contains(view, "foot") ? 16 : 14;
	} else {
		int border = view_border_px(view);
		margin = border > 6 ? border : 6;
	}
	return clamp_hit_margin(view, margin);
}

static int view_move_border_margin(const struct flux_view *view) {
	int margin = 0;
	if (view && !view->use_server_decorations) {
		/* Client-side decorated windows need a thicker move ring to be usable. */
		margin = 40;
	} else {
		int border = view_border_px(view);
		margin = border > 12 ? border : 12;
	}
	margin = clamp_hit_margin(view, margin);
	int resize_margin = view_resize_hit_margin(view);
	if (margin <= resize_margin) {
		margin = resize_margin + 1;
	}
	return clamp_hit_margin(view, margin);
}

static int view_outer_grab_pad(const struct flux_view *view) {
	int pad = 0;
	if (view && !view->use_server_decorations) {
		pad = view_app_id_contains(view, "foot") ? 16 : 14;
	} else {
		pad = 4;
	}
	return clamp_hit_margin(view, pad);
}

static void get_layout_box_or_default(struct flux_server *server, struct wlr_box *box) {
	wlr_output_layout_get_box(server->output_layout, NULL, box);
	if (box->width <= 0 || box->height <= 0) {
		box->x = 0;
		box->y = 0;
		box->width = 1280;
		box->height = 720;
	}
}

static void raise_cursor_to_top(struct flux_server *server) {
	if (server->cursor_tree) {
		wlr_scene_node_raise_to_top(&server->cursor_tree->node);
	}
}

static void schedule_all_output_frames(struct flux_server *server) {
	struct flux_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

static void view_get_geometry_box(struct flux_view *view, struct wlr_box *geo) {
	int surface_w = view->xdg_surface->surface->current.width;
	int surface_h = view->xdg_surface->surface->current.height;
	if (surface_w <= 1) {
		surface_w = 640;
	}
	if (surface_h <= 1) {
		surface_h = 480;
	}

	if (!view->use_server_decorations) {
		/*
		 * For client-side decorated windows, keep coordinates in root-surface
		 * space so pointer hit-testing aligns exactly with rendered pixels.
		 */
		geo->x = 0;
		geo->y = 0;
		geo->width = surface_w;
		geo->height = surface_h;
		return;
	}

	struct wlr_box reported = {0};
	wlr_xdg_surface_get_geometry(view->xdg_surface, &reported);
	if (reported.width > 1 && reported.height > 1) {
		/*
		 * Trust explicit non-negative xdg geometry when available. This keeps
		 * frame/hit-testing aligned to the visible window (important for CSD).
		 */
		if (reported.x >= 0 && reported.y >= 0) {
			*geo = reported;
			return;
		}
	}

	/*
	 * Fallback for clients with missing/invalid geometry. This avoids oversized
	 * ghost extents from fallback shadow bounds.
	 */
	geo->x = 0;
	geo->y = 0;
	geo->width = surface_w;
	geo->height = surface_h;
}

void place_new_view(struct flux_server *server, struct flux_view *view) {
	struct wlr_box box;
	get_layout_box_or_default(server, &box);

	const int base_x = box.x + 48;
	const int base_y = box.y + 40;
	const int step_x = 34;
	const int step_y = 26;
	const int min_tail_w = 520;
	const int min_tail_h = 380;

	if (server->next_view_x == 0 && server->next_view_y == 0) {
		server->next_view_x = base_x;
		server->next_view_y = base_y;
	}

	int max_x = box.x + box.width - min_tail_w;
	int max_y = box.y + box.height - min_tail_h;
	if (max_x < base_x) {
		max_x = base_x;
	}
	if (max_y < base_y) {
		max_y = base_y;
	}

	view->x = server->next_view_x;
	view->y = server->next_view_y;
	if (view->x < box.x) {
		view->x = box.x;
	}
	if (view->y < box.y) {
		view->y = box.y;
	}
	if (view->x > max_x) {
		view->x = max_x;
	}
	if (view->y > max_y) {
		view->y = max_y;
	}

	server->next_view_x += step_x;
	server->next_view_y += step_y;
	if (server->next_view_x > max_x || server->next_view_y > max_y) {
		server->next_view_x = base_x;
		server->next_view_y = base_y;
	}
}

void configure_new_toplevel(struct flux_server *server, struct wlr_xdg_surface *xdg_surface) {
	(void)server;
	(void)xdg_surface;
	/*
	 * Keep startup geometry client-driven. Initial configure is handled by
	 * wlroots internals; avoid forcing a pre-init configure here.
	 */
}

void view_set_frame_size(struct flux_view *view, int frame_width, int frame_height) {
	int border = view_border_px(view);
	int title_h = view_titlebar_px(view);

	if (frame_width < border * 2 + 1) {
		frame_width = border * 2 + 1;
	}
	if (frame_height < title_h + border + 1) {
		frame_height = title_h + border + 1;
	}

	view->width = frame_width;
	view->height = frame_height;

	int body_h = view->height - title_h;
	if (body_h < 1) {
		body_h = 1;
	}
	wlr_scene_rect_set_size(view->title_rect, view->width, title_h > 0 ? title_h : 1);
	wlr_scene_rect_set_size(view->left_border_rect, border > 0 ? border : 1, body_h);
	wlr_scene_rect_set_size(view->right_border_rect, border > 0 ? border : 1, body_h);
	wlr_scene_rect_set_size(view->bottom_border_rect, view->width, border > 0 ? border : 1);

	wlr_scene_node_set_position(&view->right_border_rect->node,
		view->width - (border > 0 ? border : 1), title_h);
	wlr_scene_node_set_position(&view->bottom_border_rect->node,
		0, view->height - (border > 0 ? border : 1));
	view->content_x = border - view->xdg_geo_x;
	view->content_y = title_h - view->xdg_geo_y;
	wlr_scene_node_set_position(&view->content_tree->node, view->content_x, view->content_y);

	int btn_x = view->width - border - BTN_W - BTN_PAD;
	int btn_y = (title_h - BTN_H) / 2;
	if (btn_x < 0) {
		btn_x = 0;
	}
	if (btn_y < 0) {
		btn_y = 0;
	}
	wlr_scene_rect_set_size(view->minimize_rect,
		view->use_server_decorations ? BTN_W : 1,
		view->use_server_decorations ? BTN_H : 1);
	wlr_scene_node_set_position(&view->minimize_rect->node, btn_x, btn_y);
}

void view_set_server_decorations(struct flux_view *view, bool enabled) {
	view->use_server_decorations = enabled;
	wlr_scene_node_set_enabled(&view->title_rect->node, enabled);
	wlr_scene_node_set_enabled(&view->left_border_rect->node, enabled);
	wlr_scene_node_set_enabled(&view->right_border_rect->node, enabled);
	wlr_scene_node_set_enabled(&view->bottom_border_rect->node, enabled);
	wlr_scene_node_set_enabled(&view->minimize_rect->node, enabled);
	view_update_geometry(view);
}

struct flux_view *view_from_surface(struct flux_server *server, struct wlr_surface *surface) {
	(void)server;
	if (!surface) {
		return NULL;
	}
	struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(surface);
	if (!xdg || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return NULL;
	}
	return xdg->data;
}

void view_update_geometry(struct flux_view *view) {
	struct wlr_box geo = {0};
	view_get_geometry_box(view, &geo);

	view->xdg_geo_x = geo.x;
	view->xdg_geo_y = geo.y;
	view->xdg_geo_width = geo.width;
	view->xdg_geo_height = geo.height;
	int border = view_border_px(view);
	int title_h = view_titlebar_px(view);
	view_set_frame_size(view,
		view->xdg_geo_width + border * 2,
		view->xdg_geo_height + title_h + border);
}

void view_set_visible(struct flux_view *view, bool visible) {
	wlr_scene_node_set_enabled(&view->frame_tree->node, visible);
}

struct content_transform_state {
	float scale;
	float opacity;
};

static void apply_content_transform_cb(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *data) {
	(void)sx;
	(void)sy;
	struct content_transform_state *state = data;

	int base_w = buffer->buffer_width > 0 ? buffer->buffer_width : 1;
	int base_h = buffer->buffer_height > 0 ? buffer->buffer_height : 1;
	int scaled_w = (int)lroundf((float)base_w * state->scale);
	int scaled_h = (int)lroundf((float)base_h * state->scale);
	if (scaled_w < 1) {
		scaled_w = 1;
	}
	if (scaled_h < 1) {
		scaled_h = 1;
	}

	wlr_scene_buffer_set_dest_size(buffer, scaled_w, scaled_h);
	wlr_scene_buffer_set_opacity(buffer, state->opacity);
}

static void reset_content_transform_cb(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *data) {
	(void)sx;
	(void)sy;
	(void)data;
	wlr_scene_buffer_set_dest_size(buffer, 0, 0);
	wlr_scene_buffer_set_opacity(buffer, 1.0f);
}

static void set_rect_alpha(struct wlr_scene_rect *rect, const float base[4], float alpha) {
	float color[4] = {base[0], base[1], base[2], base[3] * alpha};
	wlr_scene_rect_set_color(rect, color);
}

static float clampf(float value, float min_value, float max_value) {
	if (value < min_value) {
		return min_value;
	}
	if (value > max_value) {
		return max_value;
	}
	return value;
}

static void taskbar_target_for_animation(struct flux_view *view,
		bool include_target_if_not_minimized, double *cx, double *cy, float *scale) {
	struct wlr_box slot = {0};
	if (taskbar_predict_button_box(view->server, view, include_target_if_not_minimized, &slot)) {
		*cx = slot.x + slot.width / 2.0;
		*cy = slot.y + slot.height / 2.0;
		float sx = (float)slot.width / (float)view->width;
		float sy = (float)slot.height / (float)view->height;
		*scale = clampf(fminf(sx, sy), MINIMIZE_ANIMATION_MIN_SCALE, 0.35f);
		return;
	}

	struct wlr_box layout;
	get_layout_box_or_default(view->server, &layout);
	*cx = layout.x + layout.width / 2.0;
	*cy = layout.y + layout.height - 12.0;
	*scale = MINIMIZE_ANIMATION_MIN_SCALE;
}

static void apply_window_transform(struct flux_view *view,
		double center_x, double center_y, float scale, float alpha) {
	scale = clampf(scale, MINIMIZE_ANIMATION_MIN_SCALE, 1.0f);
	alpha = clampf(alpha, 0.15f, 1.0f);

	int scaled_w = (int)lroundf((float)view->width * scale);
	int scaled_h = (int)lroundf((float)view->height * scale);
	if (scaled_w < 1) {
		scaled_w = 1;
	}
	if (scaled_h < 1) {
		scaled_h = 1;
	}

	int frame_x = (int)lround(center_x - scaled_w / 2.0);
	int frame_y = (int)lround(center_y - scaled_h / 2.0);
	wlr_scene_node_set_position(&view->frame_tree->node, frame_x, frame_y);

	int border = (int)lroundf((float)BORDER_PX * scale);
	int title_h = (int)lroundf((float)TITLEBAR_PX * scale);
	if (border < 1) {
		border = 1;
	}
	if (title_h < 1) {
		title_h = 1;
	}

	int body_h = scaled_h - title_h;
	if (body_h < 1) {
		body_h = 1;
	}

	wlr_scene_rect_set_size(view->title_rect, scaled_w, title_h);
	wlr_scene_rect_set_size(view->left_border_rect, border, body_h);
	wlr_scene_rect_set_size(view->right_border_rect, border, body_h);
	wlr_scene_rect_set_size(view->bottom_border_rect, scaled_w, border);

	int right_x = scaled_w - border;
	if (right_x < 0) {
		right_x = 0;
	}
	int bottom_y = scaled_h - border;
	if (bottom_y < 0) {
		bottom_y = 0;
	}
	wlr_scene_node_set_position(&view->right_border_rect->node, right_x, title_h);
	wlr_scene_node_set_position(&view->bottom_border_rect->node, 0, bottom_y);
	int content_x = (int)lroundf((float)view->content_x * scale);
	int content_y = (int)lroundf((float)view->content_y * scale);
	wlr_scene_node_set_position(&view->content_tree->node, content_x, content_y);

	int btn_w = (int)lroundf((float)BTN_W * scale);
	int btn_h = (int)lroundf((float)BTN_H * scale);
	int btn_pad = (int)lroundf((float)BTN_PAD * scale);
	if (btn_w < 1) {
		btn_w = 1;
	}
	if (btn_h < 1) {
		btn_h = 1;
	}
	if (btn_pad < 1) {
		btn_pad = 1;
	}
	int btn_x = scaled_w - border - btn_w - btn_pad;
	int btn_y = (title_h - btn_h) / 2;
	if (btn_x < border) {
		btn_x = border;
	}
	if (btn_y < 0) {
		btn_y = 0;
	}
	wlr_scene_rect_set_size(view->minimize_rect, btn_w, btn_h);
	wlr_scene_node_set_position(&view->minimize_rect->node, btn_x, btn_y);

	set_rect_alpha(view->title_rect, COLOR_TITLE_INACTIVE, alpha);
	set_rect_alpha(view->left_border_rect, COLOR_BORDER, alpha);
	set_rect_alpha(view->right_border_rect, COLOR_BORDER, alpha);
	set_rect_alpha(view->bottom_border_rect, COLOR_BORDER, alpha);
	set_rect_alpha(view->minimize_rect, COLOR_MIN_BUTTON, alpha);

	struct content_transform_state state = {
		.scale = scale,
		.opacity = alpha,
	};
	wlr_scene_node_for_each_buffer(&view->content_tree->node, apply_content_transform_cb, &state);
}

static void apply_running_animation_state(struct flux_view *view, float progress) {
	progress = clampf(progress, 0.0f, 1.0f);
	float eased = progress * progress * (3.0f - 2.0f * progress);

	double cx = view->anim_from_cx + (view->anim_to_cx - view->anim_from_cx) * eased;
	double cy = view->anim_from_cy + (view->anim_to_cy - view->anim_from_cy) * eased;
	float scale = view->anim_from_scale + (view->anim_to_scale - view->anim_from_scale) * eased;
	float alpha = view->anim_from_alpha + (view->anim_to_alpha - view->anim_from_alpha) * eased;

	apply_window_transform(view, cx, cy, scale, alpha);
}

static void reset_window_animation_state(struct flux_view *view) {
	wlr_scene_node_set_position(&view->frame_tree->node, view->x, view->y);
	view_update_geometry(view);
	wlr_scene_rect_set_color(view->title_rect, COLOR_TITLE_INACTIVE);
	wlr_scene_rect_set_color(view->left_border_rect, COLOR_BORDER);
	wlr_scene_rect_set_color(view->right_border_rect, COLOR_BORDER);
	wlr_scene_rect_set_color(view->bottom_border_rect, COLOR_BORDER);
	wlr_scene_rect_set_color(view->minimize_rect, COLOR_MIN_BUTTON);
	wlr_scene_node_for_each_buffer(&view->content_tree->node, reset_content_transform_cb, NULL);
}

void view_begin_minimize_animation(struct flux_view *view, uint32_t time_msec) {
	if (!view || !view->mapped || view->minimized ||
			view->minimizing_animation || view->restoring_animation) {
		return;
	}

	view->minimizing_animation = true;
	view->minimize_animation_start_msec = time_msec;
	view->restoring_animation = false;
	view->restore_animation_start_msec = 0;

	if (view->xdg_surface && view->xdg_surface->toplevel) {
		wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, false);
	}
	wlr_scene_rect_set_color(view->title_rect, COLOR_TITLE_INACTIVE);

	struct flux_server *server = view->server;
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	if (focused && view_from_surface(server, focused) == view) {
		wlr_seat_keyboard_clear_focus(server->seat);
	}

	double to_cx = 0.0, to_cy = 0.0;
	float to_scale = MINIMIZE_ANIMATION_MIN_SCALE;
	taskbar_target_for_animation(view, true, &to_cx, &to_cy, &to_scale);

	view->anim_from_cx = view->x + view->width / 2.0;
	view->anim_from_cy = view->y + view->height / 2.0;
	view->anim_to_cx = to_cx;
	view->anim_to_cy = to_cy;
	view->anim_from_scale = 1.0f;
	view->anim_to_scale = to_scale;
	view->anim_from_alpha = 1.0f;
	view->anim_to_alpha = 0.35f;

	apply_running_animation_state(view, 0.0f);

	schedule_all_output_frames(server);
}

void view_begin_restore_animation(struct flux_view *view, uint32_t time_msec) {
	if (!view || !view->mapped || !view->minimized ||
			view->minimizing_animation || view->restoring_animation) {
		return;
	}

	struct flux_server *server = view->server;

	double from_cx = 0.0, from_cy = 0.0;
	float from_scale = MINIMIZE_ANIMATION_MIN_SCALE;
	bool have_slot = false;

	if (view->taskbar_visible && view->taskbar_width > 0 && view->taskbar_height > 0) {
		struct wlr_box slot = {
			.x = view->taskbar_x,
			.y = view->taskbar_y,
			.width = view->taskbar_width,
			.height = view->taskbar_height,
		};
		from_cx = slot.x + slot.width / 2.0;
		from_cy = slot.y + slot.height / 2.0;
		float sx = (float)slot.width / (float)view->width;
		float sy = (float)slot.height / (float)view->height;
		from_scale = clampf(fminf(sx, sy), MINIMIZE_ANIMATION_MIN_SCALE, 0.35f);
		have_slot = true;
	}

	if (!have_slot) {
		taskbar_target_for_animation(view, false, &from_cx, &from_cy, &from_scale);
	}

	view->minimized = false;
	view->restoring_animation = true;
	view->restore_animation_start_msec = time_msec;
	view->minimizing_animation = false;
	view->minimize_animation_start_msec = 0;

	view->anim_from_cx = from_cx;
	view->anim_from_cy = from_cy;
	view->anim_to_cx = view->x + view->width / 2.0;
	view->anim_to_cy = view->y + view->height / 2.0;
	view->anim_from_scale = from_scale;
	view->anim_to_scale = 1.0f;
	view->anim_from_alpha = 0.35f;
	view->anim_to_alpha = 1.0f;

	view_set_visible(view, true);
	apply_running_animation_state(view, 0.0f);
	taskbar_mark_dirty(server);
	schedule_all_output_frames(server);
}

bool view_tick_animations(struct flux_server *server, uint32_t time_msec) {
	bool any_running = false;

	struct flux_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped) {
			continue;
		}

		if (view->minimizing_animation) {
			uint32_t elapsed = time_msec - view->minimize_animation_start_msec;
			float progress = (float)elapsed / (float)MINIMIZE_ANIMATION_DURATION_MS;
			if (progress >= 1.0f) {
				view->minimizing_animation = false;
				reset_window_animation_state(view);
				view->minimized = true;
				view_set_visible(view, false);
				taskbar_mark_dirty(server);
				continue;
			}

			any_running = true;
			apply_running_animation_state(view, progress);
			continue;
		}

		if (view->restoring_animation) {
			uint32_t elapsed = time_msec - view->restore_animation_start_msec;
			float progress = (float)elapsed / (float)RESTORE_ANIMATION_DURATION_MS;
			if (progress >= 1.0f) {
				view->restoring_animation = false;
				reset_window_animation_state(view);
				focus_view(view, view->xdg_surface->surface);
				continue;
			}

			any_running = true;
			apply_running_animation_state(view, progress);
		}
	}

	return any_running;
}

void focus_view(struct flux_view *view, struct wlr_surface *surface) {
	if (!view || view->minimized || view->minimizing_animation ||
			view->restoring_animation || !view->mapped) {
		return;
	}

	struct flux_server *server = view->server;
	struct wlr_surface *prev = server->seat->keyboard_state.focused_surface;
	struct flux_view *prev_view = view_from_surface(server, prev);
	if (prev == surface) {
		return;
	}

	if (prev_view && prev_view != view && prev_view->xdg_surface &&
			prev_view->xdg_surface->toplevel) {
		wlr_xdg_toplevel_set_activated(prev_view->xdg_surface->toplevel, false);
	}

	struct flux_view *iter;
	wl_list_for_each(iter, &server->views, link) {
		if (!iter->mapped || iter->minimized ||
				iter->minimizing_animation || iter->restoring_animation) {
			continue;
		}
		const float *color = (iter == view) ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE;
		wlr_scene_rect_set_color(iter->title_rect, color);
	}

	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
	wlr_scene_node_raise_to_top(&view->frame_tree->node);
	raise_cursor_to_top(server);
	if (view->xdg_surface && view->xdg_surface->toplevel) {
		wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(server->seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

struct flux_view *view_at(struct flux_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct flux_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped || view->minimized ||
				view->minimizing_animation || view->restoring_animation) {
			continue;
		}

		if (lx < view->x || ly < view->y ||
			lx >= view->x + view->width || ly >= view->y + view->height) {
			continue;
		}

		double local_x = lx - (view->x + view->content_x);
		double local_y = ly - (view->y + view->content_y);
		struct wlr_surface *hit = wlr_xdg_surface_surface_at(view->xdg_surface,
			local_x, local_y, sx, sy);
		if (hit) {
			*surface = hit;
			return view;
		}

		if (!view->use_server_decorations) {
			*surface = NULL;
			return NULL;
		}

		*surface = NULL;
		*sx = local_x;
		*sy = local_y;
		return view;
	}

	*surface = NULL;
	return NULL;
}

struct flux_view *view_frame_at(struct flux_server *server, double lx, double ly) {
	struct flux_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped || view->minimized ||
				view->minimizing_animation || view->restoring_animation) {
			continue;
		}

		int pad = view_outer_grab_pad(view);
		if (lx < view->x - pad || ly < view->y - pad ||
				lx >= view->x + view->width + pad ||
				ly >= view->y + view->height + pad) {
			continue;
		}

		return view;
	}

	return NULL;
}

uint32_t view_resize_edges_at(struct flux_view *view, double lx, double ly) {
	if (!view) {
		return WLR_EDGE_NONE;
	}

	/*
	 * Keep resize hit-zones wide enough to grab easily, including CSD windows
	 * where compositor borders are hidden.
	 */
	int margin = view_resize_hit_margin(view);
	if (lx < view->x - margin || ly < view->y - margin ||
			lx >= view->x + view->width + margin ||
			ly >= view->y + view->height + margin) {
		return WLR_EDGE_NONE;
	}

	double local_x = lx - view->x;
	double local_y = ly - view->y;
	bool left = local_x < margin;
	bool right = local_x >= view->width - margin;
	bool top = local_y < margin;
	bool bottom = local_y >= view->height - margin;

	uint32_t edges = WLR_EDGE_NONE;
	if (left) {
		edges |= WLR_EDGE_LEFT;
	}
	if (right) {
		edges |= WLR_EDGE_RIGHT;
	}
	if (top) {
		edges |= WLR_EDGE_TOP;
	}
	if (bottom) {
		edges |= WLR_EDGE_BOTTOM;
	}
	return edges;
}

bool view_point_in_frame_border(struct flux_view *view, double lx, double ly) {
	if (!view) {
		return false;
	}
	int outer_pad = view_outer_grab_pad(view);
	if (lx < view->x - outer_pad || ly < view->y - outer_pad ||
			lx >= view->x + view->width + outer_pad ||
			ly >= view->y + view->height + outer_pad) {
		return false;
	}

	int move_margin = view_move_border_margin(view);
	int resize_margin = view_resize_hit_margin(view);
	double local_x = lx - view->x;
	double local_y = ly - view->y;
	bool in_move_ring = local_x < move_margin ||
		local_x >= view->width - move_margin ||
		local_y < move_margin ||
		local_y >= view->height - move_margin;
	if (!in_move_ring) {
		return false;
	}

	bool in_resize_ring = local_x < resize_margin ||
		local_x >= view->width - resize_margin ||
		local_y < resize_margin ||
		local_y >= view->height - resize_margin;
	return !in_resize_ring;
}

bool point_in_minimize_button(struct flux_view *view, double lx, double ly) {
	if (!view->use_server_decorations) {
		return false;
	}
	int border = view_border_px(view);
	int title_h = view_titlebar_px(view);
	int btn_x = view->x + view->width - border - BTN_W - BTN_PAD;
	int btn_y = view->y + (title_h - BTN_H) / 2;
	return lx >= btn_x && ly >= btn_y && lx < btn_x + BTN_W && ly < btn_y + BTN_H;
}

bool point_in_titlebar_drag_region(struct flux_view *view, double lx, double ly) {
	if (!view->use_server_decorations) {
		return false;
	}
	int title_h = view_titlebar_px(view);
	if (ly < view->y || ly >= view->y + title_h || lx < view->x || lx >= view->x + view->width) {
		return false;
	}
	if (point_in_minimize_button(view, lx, ly)) {
		return false;
	}
	return true;
}
