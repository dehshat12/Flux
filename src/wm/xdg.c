#include "flux.h"

struct flux_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener request_mode;
	struct wl_listener destroy;
};

static bool xdg_surface_ready(struct wlr_xdg_surface *xdg_surface) {
	return xdg_surface && xdg_surface->initialized;
}

static enum wlr_xdg_toplevel_decoration_v1_mode choose_mode_for_view(struct flux_view *view) {
	(void)view;
	/*
 * Flux currently does not draw compositor-side window decorations.
	 * Always ask clients to use their own decoration style.
	 */
	return WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
}

static void apply_decoration_mode_to_view(struct flux_view *view) {
	if (!view) {
		return;
	}

	enum wlr_xdg_toplevel_decoration_v1_mode mode = choose_mode_for_view(view);
	bool use_server = mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	view_set_server_decorations(view, use_server);

	if (view->xdg_decoration && xdg_surface_ready(view->xdg_surface)) {
		wlr_xdg_toplevel_decoration_v1_set_mode(view->xdg_decoration, mode);
	}
}

static void decoration_request_mode_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_decoration *dec =
		wl_container_of(listener, dec, request_mode);
	if (!dec->decoration || !dec->decoration->toplevel ||
			!dec->decoration->toplevel->base) {
		return;
	}
	struct flux_view *view = dec->decoration->toplevel->base->data;
	apply_decoration_mode_to_view(view);
}

static void decoration_destroy_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_decoration *dec =
		wl_container_of(listener, dec, destroy);
	if (dec->decoration && dec->decoration->toplevel &&
			dec->decoration->toplevel->base) {
		struct flux_view *view = dec->decoration->toplevel->base->data;
		if (view && view->xdg_decoration == dec->decoration) {
			view->xdg_decoration = NULL;
		}
	}
	wl_list_remove(&dec->request_mode.link);
	wl_list_remove(&dec->destroy.link);
	free(dec);
}

static bool validate_interactive_request(struct flux_view *view,
		struct wlr_seat_client *seat_client, uint32_t serial) {
	if (!view || !view->server || !seat_client) {
		return false;
	}

	if (!view->mapped || view->minimized ||
			view->minimizing_animation || view->restoring_animation) {
		return false;
	}

	return wlr_seat_validate_pointer_grab_serial(
		view->server->seat, view->xdg_surface->surface, serial);
}

static void view_request_move_notify(struct wl_listener *listener, void *data) {
	struct flux_view *view = wl_container_of(listener, view, request_move);
	struct wlr_xdg_toplevel_move_event *event = data;
	struct flux_server *server = view->server;

	if (!validate_interactive_request(view, event->seat, event->serial)) {
		wlr_log(WLR_INFO, "xdg request_move rejected: app_id=%s title=%s",
			view->xdg_surface->toplevel && view->xdg_surface->toplevel->app_id ?
				view->xdg_surface->toplevel->app_id : "(null)",
			view->xdg_surface->toplevel && view->xdg_surface->toplevel->title ?
				view->xdg_surface->toplevel->title : "(null)");
		return;
	}

	wlr_log(WLR_INFO, "xdg request_move accepted: app_id=%s title=%s",
		view->xdg_surface->toplevel && view->xdg_surface->toplevel->app_id ?
			view->xdg_surface->toplevel->app_id : "(null)",
		view->xdg_surface->toplevel && view->xdg_surface->toplevel->title ?
			view->xdg_surface->toplevel->title : "(null)");
	focus_view(view, view->xdg_surface->surface);
	server->cursor_mode = CURSOR_MOVE;
	server->grabbed_view = view;
	server->interactive_grab_from_client = true;
	server->grab_x = server->cursor_x - view->x;
	server->grab_y = server->cursor_y - view->y;
	server->suppress_button_until_release = true;
}

static void view_request_resize_notify(struct wl_listener *listener, void *data) {
	struct flux_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct flux_server *server = view->server;

	if (!validate_interactive_request(view, event->seat, event->serial)) {
		return;
	}
	if (event->edges == WLR_EDGE_NONE) {
		return;
	}

	focus_view(view, view->xdg_surface->surface);
	server->cursor_mode = CURSOR_RESIZE;
	server->grabbed_view = view;
	server->interactive_grab_from_client = true;
	server->resize_edges = event->edges;
	server->resize_init_x = view->x;
	server->resize_init_y = view->y;
	server->resize_init_width = view->width;
	server->resize_init_height = view->height;
	server->resize_cursor_start_x = server->cursor_x;
	server->resize_cursor_start_y = server->cursor_y;
	server->suppress_button_until_release = true;
}

void xdg_activation_request_activate_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(
		listener, server, xdg_activation_request_activate);
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	struct flux_view *view = view_from_surface(server, event->surface);
	if (!view || !view->mapped || view->minimized ||
			view->minimizing_animation || view->restoring_animation) {
		return;
	}
	focus_view(view, event->surface);
}

void xdg_decoration_new_toplevel_notify(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
	struct flux_decoration *dec = calloc(1, sizeof(*dec));
	if (dec) {
		dec->decoration = decoration;
		dec->request_mode.notify = decoration_request_mode_notify;
		wl_signal_add(&decoration->events.request_mode, &dec->request_mode);
		dec->destroy.notify = decoration_destroy_notify;
		wl_signal_add(&decoration->events.destroy, &dec->destroy);
		decoration->data = dec;
	} else {
		wlr_log(WLR_ERROR,
			"failed to allocate decoration listener state; forcing client-side once");
	}
	if (decoration && decoration->toplevel && decoration->toplevel->base) {
		struct flux_view *view = decoration->toplevel->base->data;
		if (view) {
			view->xdg_decoration = decoration;
			apply_decoration_mode_to_view(view);
		}
	}
}

static void view_map_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_view *view = wl_container_of(listener, view, map);
	apply_decoration_mode_to_view(view);
	view->mapped = true;
	view->minimized = false;
	view->minimizing_animation = false;
	view->restoring_animation = false;
	wlr_log(WLR_INFO, "view map: app_id=%s title=%s",
		view->xdg_surface->toplevel && view->xdg_surface->toplevel->app_id ?
			view->xdg_surface->toplevel->app_id : "(null)",
		view->xdg_surface->toplevel && view->xdg_surface->toplevel->title ?
			view->xdg_surface->toplevel->title : "(null)");
	view_update_geometry(view);
	wlr_log(WLR_INFO, "view geometry: app_id=%s geo=(%d,%d %dx%d) content=(%d,%d) frame=(%d,%d %dx%d) ssd=%d",
		view->xdg_surface->toplevel && view->xdg_surface->toplevel->app_id ?
			view->xdg_surface->toplevel->app_id : "(null)",
		view->xdg_geo_x, view->xdg_geo_y, view->xdg_geo_width, view->xdg_geo_height,
		view->content_x, view->content_y, view->x, view->y, view->width, view->height,
		view->use_server_decorations ? 1 : 0);
	view_set_visible(view, true);
	focus_view(view, view->xdg_surface->surface);
	taskbar_mark_dirty(view->server);
}

static void view_unmap_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_view *view = wl_container_of(listener, view, unmap);
	if (view->server->pressed_taskbar_view == view) {
		view->server->pressed_taskbar_view = NULL;
	}
	view->mapped = false;
	view->minimizing_animation = false;
	view->restoring_animation = false;
	wlr_log(WLR_INFO, "view unmap");
	view_set_visible(view, false);
	taskbar_mark_dirty(view->server);
}

static void view_commit_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_view *view = wl_container_of(listener, view, commit);
	if (!view->mapped) {
		/*
		 * New xdg-toplevels need an initial configure before they can map.
		 * Only schedule once the surface is initialized to avoid wlroots
		 * "uninitialized xdg_surface" protocol errors.
		 */
		if (xdg_surface_ready(view->xdg_surface) && !view->xdg_surface->configured) {
			wlr_xdg_surface_schedule_configure(view->xdg_surface);
		}
		return;
	}

	if (view->minimized || view->minimizing_animation || view->restoring_animation) {
		return;
	}
	view_update_geometry(view);
}

static void view_destroy_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_view *view = wl_container_of(listener, view, destroy);
	wlr_log(WLR_INFO, "view destroy");
	if (view->server->pressed_taskbar_view == view) {
		view->server->pressed_taskbar_view = NULL;
	}
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->set_app_id.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->link);
	taskbar_mark_dirty(view->server);
	free(view);
}

static void view_set_title_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_view *view = wl_container_of(listener, view, set_title);
	taskbar_mark_dirty(view->server);
}

static void view_set_app_id_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_view *view = wl_container_of(listener, view, set_app_id);
	apply_decoration_mode_to_view(view);
	taskbar_mark_dirty(view->server);
}

void new_xdg_toplevel_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;
	struct wlr_xdg_surface *xdg_surface = xdg_toplevel->base;

	struct flux_view *view = calloc(1, sizeof(*view));
	view->server = server;
	view->xdg_surface = xdg_surface;
	xdg_surface->data = view;
	place_new_view(server, view);
	view->mapped = false;
	view->minimized = false;
	view->use_server_decorations = false;
	view->xdg_decoration = NULL;
	wlr_log(WLR_INFO, "new xdg toplevel: app_id=%s title=%s",
		xdg_surface->toplevel && xdg_surface->toplevel->app_id ?
			xdg_surface->toplevel->app_id : "(null)",
		xdg_surface->toplevel && xdg_surface->toplevel->title ?
			xdg_surface->toplevel->title : "(null)");

	view->frame_tree = wlr_scene_tree_create(&server->scene->tree);
	wlr_scene_node_set_position(&view->frame_tree->node, view->x, view->y);

	view->title_rect = wlr_scene_rect_create(view->frame_tree, 320, TITLEBAR_PX, COLOR_TITLE_INACTIVE);
	view->left_border_rect = wlr_scene_rect_create(view->frame_tree,
		BORDER_PX, 100, COLOR_BORDER);
	view->right_border_rect = wlr_scene_rect_create(view->frame_tree,
		BORDER_PX, 100, COLOR_BORDER);
	view->bottom_border_rect = wlr_scene_rect_create(view->frame_tree,
		320, BORDER_PX, COLOR_BORDER);
	view->minimize_rect = wlr_scene_rect_create(view->frame_tree,
		BTN_W, BTN_H, COLOR_MIN_BUTTON);
	view->content_tree = wlr_scene_tree_create(view->frame_tree);

	wlr_scene_xdg_surface_create(view->content_tree, xdg_surface);
	view_set_server_decorations(view, view->use_server_decorations);
	view_set_visible(view, false);
	configure_new_toplevel(server, xdg_surface);

	view->map.notify = view_map_notify;
	wl_signal_add(&xdg_surface->surface->events.map, &view->map);
	view->unmap.notify = view_unmap_notify;
	wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
	view->destroy.notify = view_destroy_notify;
	wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
	view->commit.notify = view_commit_notify;
	wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);
	view->set_title.notify = view_set_title_notify;
	wl_signal_add(&xdg_toplevel->events.set_title, &view->set_title);
	view->set_app_id.notify = view_set_app_id_notify;
	wl_signal_add(&xdg_toplevel->events.set_app_id, &view->set_app_id);
	view->request_move.notify = view_request_move_notify;
	wl_signal_add(&xdg_toplevel->events.request_move, &view->request_move);
	view->request_resize.notify = view_request_resize_notify;
	wl_signal_add(&xdg_toplevel->events.request_resize, &view->request_resize);

	wl_list_insert(&server->views, &view->link);
	taskbar_mark_dirty(server);
}
