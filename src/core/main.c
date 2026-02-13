#include "flux.h"

static bool env_is_set(const char *name) {
	const char *value = getenv(name);
	return value && value[0] != '\0';
}

static bool file_contains_token(const char *path, const char *token) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}

	char buf[256];
	bool found = false;
	while (fgets(buf, sizeof(buf), f) != NULL) {
		if (strstr(buf, token) != NULL) {
			found = true;
			break;
		}
	}

	fclose(f);
	return found;
}

static bool running_on_parallels(void) {
	return file_contains_token("/sys/class/dmi/id/sys_vendor", "Parallels") ||
		file_contains_token("/sys/class/dmi/id/product_name", "Parallels");
}

static void enable_dumb_graphics_environment(bool force_pixman_renderer) {
	if (force_pixman_renderer || !env_is_set("WLR_RENDERER")) {
		setenv("WLR_RENDERER", "pixman", 1);
	}
	setenv("WLR_NO_HARDWARE_CURSORS", "1", 1);

	/*
	 * Help launched clients survive weak/unsupported GL stacks by defaulting to
	 * software rendering when dumb graphics mode is active.
	 */
	if (!env_is_set("LIBGL_ALWAYS_SOFTWARE")) {
		setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
	}
	if (!env_is_set("MESA_LOADER_DRIVER_OVERRIDE")) {
		setenv("MESA_LOADER_DRIVER_OVERRIDE", "llvmpipe", 1);
	}
}

static void configure_client_environment_defaults(void) {
	/*
	 * Let clients keep their own decoration policy.
	 * Do not force GTK/Qt CSD off.
	 */
}

static bool create_renderer_and_allocator(struct flux_server *server) {
	server->renderer = wlr_renderer_autocreate(server->backend);
	if (!server->renderer) {
		return false;
	}

	server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
	if (!server->allocator) {
		wlr_renderer_destroy(server->renderer);
		server->renderer = NULL;
		return false;
	}

	if (!wlr_renderer_init_wl_display(server->renderer, server->display)) {
		wlr_allocator_destroy(server->allocator);
		server->allocator = NULL;
		wlr_renderer_destroy(server->renderer);
		server->renderer = NULL;
		return false;
	}

	return true;
}

int main(void) {
	setup_child_reaping();
	init_logging();
	atexit(close_logging);
	wlr_log_init(WLR_INFO, flux_log_callback);
	const char *log_path = flux_log_path();
	if (log_path && log_path[0] != '\0') {
		wlr_log(WLR_INFO, "logging to %s", log_path);
	} else {
		wlr_log(WLR_ERROR, "failed to open log file, logging only to stderr");
	}

	struct flux_server server = {0};
	wl_list_init(&server.outputs);
	wl_list_init(&server.keyboards);
	wl_list_init(&server.views);
	server.cursor_hotspot_x = env_int("FLUX_CURSOR_HOTSPOT_X", 0);
	server.cursor_hotspot_y = env_int("FLUX_CURSOR_HOTSPOT_Y", 0);
	bool on_parallels = running_on_parallels();
	bool no_hw_cursors = on_parallels;
	bool dumb_graphics_mode = env_int("FLUX_DUMB_GRAPHICS", 0) != 0;
	server.keybind_mod_mask = parse_keybind_mod_mask();
	server.use_drawn_cursor = on_parallels;
	server.cursor_mode = CURSOR_PASSTHROUGH;

	if (on_parallels && !dumb_graphics_mode) {
		wlr_log(WLR_INFO, "Parallels VM detected; forcing dumb graphics mode for pointer stability");
		dumb_graphics_mode = true;
	}

	if (dumb_graphics_mode) {
		if (!env_is_set("WLR_RENDERER")) {
			wlr_log(WLR_INFO, "dumb graphics mode enabled: forcing WLR_RENDERER=pixman");
		} else {
			wlr_log(WLR_INFO, "dumb graphics mode requested; keeping existing WLR_RENDERER=%s",
				getenv("WLR_RENDERER"));
		}

		enable_dumb_graphics_environment(false);
		no_hw_cursors = true;
		server.use_drawn_cursor = true;
	} else if (!env_is_set("WLR_RENDERER")) {
		wlr_log(WLR_INFO, "graphics mode: wlroots auto renderer selection");
	}

	if (on_parallels) {
		no_hw_cursors = true;
		wlr_log(WLR_INFO, "startup detected Parallels VM; enabling drawn-cursor compatibility");
	}

	if (no_hw_cursors) {
		setenv("WLR_NO_HARDWARE_CURSORS", "1", 1);
	}

	wlr_log(WLR_INFO, "hardware cursor planes: %s",
		no_hw_cursors ? "disabled" : "enabled");
	const char *renderer_env = getenv("WLR_RENDERER");
	wlr_log(WLR_INFO, "renderer backend requested: %s",
		renderer_env && renderer_env[0] != '\0' ? renderer_env : "autocreate");
	wlr_log(WLR_INFO, "keybind modifier mask: 0x%x", server.keybind_mod_mask);
	wlr_log(WLR_INFO, "cursor mode: %s",
		server.use_drawn_cursor ? "drawn" : "theme/client");
	configure_client_environment_defaults();

	server.display = wl_display_create();
	if (!server.display) {
		wlr_log(WLR_ERROR, "failed to create wayland display");
		return 1;
	}

	struct wl_event_loop *event_loop = wl_display_get_event_loop(server.display);
	server.sigint_source = wl_event_loop_add_signal(
		event_loop, SIGINT, handle_terminate_signal, &server);
	server.sigterm_source = wl_event_loop_add_signal(
		event_loop, SIGTERM, handle_terminate_signal, &server);
	if (!server.sigint_source || !server.sigterm_source) {
		wlr_log(WLR_ERROR, "failed to register signal handlers");
		wl_display_destroy(server.display);
		return 1;
	}

	server.backend = wlr_backend_autocreate(event_loop, NULL);
	if (!server.backend) {
		wlr_log(WLR_ERROR, "failed to create backend");
		wl_display_destroy(server.display);
		return 1;
	}

	if (!create_renderer_and_allocator(&server)) {
		const char *active_renderer = getenv("WLR_RENDERER");
		bool already_pixman = active_renderer && strcmp(active_renderer, "pixman") == 0;
		if (!already_pixman) {
			wlr_log(WLR_ERROR,
				"unsupported graphics path detected, retrying with dumb graphics (pixman)");
			enable_dumb_graphics_environment(true);
			no_hw_cursors = true;
			server.use_drawn_cursor = true;
			if (!create_renderer_and_allocator(&server)) {
				wlr_log(WLR_ERROR, "dumb graphics fallback failed");
			}
		}
	}

	if (!server.renderer || !server.allocator) {
		wlr_log(WLR_ERROR, "failed to create renderer/allocator");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	const char *active_renderer = getenv("WLR_RENDERER");
	wlr_log(WLR_INFO, "renderer backend env after init: %s",
		active_renderer && active_renderer[0] != '\0' ? active_renderer : "autocreate");

	wlr_compositor_create(server.display, 6, server.renderer);
	server.subcompositor = wlr_subcompositor_create(server.display);
	if (!server.subcompositor) {
		wlr_log(WLR_ERROR, "failed to create subcompositor");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}
	server.primary_selection_v1 =
		wlr_primary_selection_v1_device_manager_create(server.display);
	server.xdg_activation_v1 = wlr_xdg_activation_v1_create(server.display);
	server.viewporter = wlr_viewporter_create(server.display);
	server.fractional_scale_v1 =
		wlr_fractional_scale_manager_v1_create(server.display, 1);
	server.cursor_shape_v1 = wlr_cursor_shape_manager_v1_create(server.display, 1);
	server.text_input_v3 = wlr_text_input_manager_v3_create(server.display);
	server.input_method_v2 = wlr_input_method_manager_v2_create(server.display);
	server.xdg_decoration_v1 = wlr_xdg_decoration_manager_v1_create(server.display);
	if (!server.primary_selection_v1 || !server.xdg_activation_v1 ||
			!server.viewporter || !server.fractional_scale_v1 ||
			!server.cursor_shape_v1 || !server.text_input_v3 ||
			!server.input_method_v2 || !server.xdg_decoration_v1) {
		wlr_log(WLR_ERROR, "failed to create one or more protocol managers");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}
	wlr_data_device_manager_create(server.display);

	server.output_layout = wlr_output_layout_create(server.display);
	server.scene = wlr_scene_create();
	wlr_scene_attach_output_layout(server.scene, server.output_layout);
	taskbar_init(&server);

	server.seat = wlr_seat_create(server.display, "seat0");
	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
	server.xcursor_manager = wlr_xcursor_manager_create(NULL, 24);
	if (!server.xcursor_manager) {
		wlr_log(WLR_ERROR, "failed to create xcursor manager");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}
	wlr_xcursor_manager_load(server.xcursor_manager, 1.0f);

	if (server.use_drawn_cursor) {
		// Explicitly hide wlroots cursor image when compositor renders its own pointer.
		wlr_cursor_unset_image(server.cursor);
		create_cursor_pointer(&server);
	} else {
		apply_default_cursor(&server);
	}

	server.xdg_shell = wlr_xdg_shell_create(server.display, 3);

	server.new_output.notify = new_output_notify;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.new_input.notify = new_input_notify;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);

	server.new_xdg_toplevel.notify = new_xdg_toplevel_notify;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.xdg_activation_request_activate.notify = xdg_activation_request_activate_notify;
	wl_signal_add(&server.xdg_activation_v1->events.request_activate,
		&server.xdg_activation_request_activate);
	server.xdg_decoration_new_toplevel.notify = xdg_decoration_new_toplevel_notify;
	wl_signal_add(&server.xdg_decoration_v1->events.new_toplevel_decoration,
		&server.xdg_decoration_new_toplevel);
	server.cursor_shape_request_set_shape.notify = cursor_shape_request_set_shape_notify;
	wl_signal_add(&server.cursor_shape_v1->events.request_set_shape,
		&server.cursor_shape_request_set_shape);

	server.cursor_motion.notify = cursor_motion_notify;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = cursor_motion_absolute_notify;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
	server.cursor_button.notify = cursor_button_notify;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = cursor_axis_notify;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = cursor_frame_notify;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);
	server.seat_request_set_cursor.notify = seat_request_set_cursor_notify;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.seat_request_set_cursor);

	const char *socket = wl_display_add_socket_auto(server.display);
	if (!socket) {
		wlr_log(WLR_ERROR, "failed to create wayland socket");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	wlr_log(WLR_INFO, "starting flux compositor on WAYLAND_DISPLAY=%s", socket);

	if (!wlr_backend_start(server.backend)) {
		wlr_log(WLR_ERROR, "failed to start backend");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	wl_display_run(server.display);

	wl_display_destroy_clients(server.display);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.display);
	wlr_log(WLR_INFO, "flux compositor exited");
	return 0;
}
