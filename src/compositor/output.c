#include "flux.h"

static void update_output_background(struct flux_output *output) {
	if (!output || !output->background_rect) {
		return;
	}

	struct wlr_box box = {0};
	wlr_output_layout_get_box(output->server->output_layout, output->wlr_output, &box);
	if (box.width <= 0 || box.height <= 0) {
		return;
	}

	wlr_scene_rect_set_size(output->background_rect, box.width, box.height);
	wlr_scene_node_set_position(&output->background_rect->node, box.x, box.y);
	wlr_scene_node_lower_to_bottom(&output->background_rect->node);
}

static void output_frame_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_output *output = wl_container_of(listener, output, frame);
	struct flux_server *server = output->server;

	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(server->scene, output->wlr_output);
	if (!scene_output) {
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	uint32_t now_msec = (uint32_t)(((uint64_t)now.tv_sec * 1000ull) +
		((uint64_t)now.tv_nsec / 1000000ull));

	update_output_background(output);
	bool animating = view_tick_animations(server, now_msec);
	taskbar_update(server);

	wlr_scene_output_commit(scene_output, NULL);
	wlr_scene_output_send_frame_done(scene_output, &now);

	if (animating) {
		struct flux_output *iter;
		wl_list_for_each(iter, &server->outputs, link) {
			wlr_output_schedule_frame(iter->wlr_output);
		}
	}
}

static void output_destroy_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_output *output = wl_container_of(listener, output, destroy);
	if (output->background_rect) {
		wlr_scene_node_destroy(&output->background_rect->node);
		output->background_rect = NULL;
	}
	taskbar_mark_dirty(output->server);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

void new_output_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
		wlr_log(WLR_ERROR, "output init render failed");
		return;
	}

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}
	if (!wlr_output_commit_state(wlr_output, &state)) {
		wlr_output_state_finish(&state);
		wlr_log(WLR_ERROR, "output commit failed");
		return;
	}
	wlr_output_state_finish(&state);

	wlr_output_layout_add_auto(server->output_layout, wlr_output);
	wlr_scene_output_create(server->scene, wlr_output);
	taskbar_mark_dirty(server);

	struct flux_output *output = calloc(1, sizeof(*output));
	output->server = server;
	output->wlr_output = wlr_output;
	output->background_rect =
		wlr_scene_rect_create(&server->scene->tree, 1, 1, COLOR_BACKGROUND);
	update_output_background(output);

	if (server->xcursor_manager) {
		wlr_xcursor_manager_load(server->xcursor_manager, wlr_output->scale);
		apply_default_cursor(server);
	}

	/* Ensure cursor starts inside the newly active output bounds. */
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
	if (box.width > 0 && box.height > 0) {
		double cx = box.x + box.width / 2.0;
		double cy = box.y + box.height / 2.0;
		wlr_cursor_warp_closest(server->cursor, NULL, cx, cy);
		server->cursor_x = server->cursor->x;
		server->cursor_y = server->cursor->y;
		wlr_log(WLR_INFO, "cursor centered on output at %.0f,%.0f", cx, cy);
	}

	output->frame.notify = output_frame_notify;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->destroy.notify = output_destroy_notify;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	if (server->use_drawn_cursor) {
		if (server->cursor_tree) {
			wlr_scene_node_destroy(&server->cursor_tree->node);
			server->cursor_tree = NULL;
		}
		create_cursor_pointer(server);
		if (server->cursor_tree) {
			wlr_scene_node_set_position(&server->cursor_tree->node,
				(int)server->cursor_x - server->cursor_hotspot_x,
				(int)server->cursor_y - server->cursor_hotspot_y);
		}
	}
}
