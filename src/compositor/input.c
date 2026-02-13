#include "flux.h"

static void log_libinput_status(const char *device_name, const char *setting,
		enum libinput_config_status status) {
	if (status == LIBINPUT_CONFIG_STATUS_SUCCESS ||
			status == LIBINPUT_CONFIG_STATUS_UNSUPPORTED) {
		return;
	}
	wlr_log(WLR_INFO, "libinput: %s: %s => %s",
		device_name ? device_name : "unknown-device", setting,
		libinput_config_status_to_str(status));
}

static void configure_libinput_device(struct wlr_input_device *device,
		struct libinput_device *libinput) {
	const char *name = device->name ? device->name : "unknown-device";

	int tap_finger_count = libinput_device_config_tap_get_finger_count(libinput);
	bool is_touchpad = tap_finger_count > 0;
	int tap_enabled = 0;
	int tap_drag_enabled = 0;
	int tap_drag_lock_enabled = 0;
	int prefer_flat_profile = 1;
	double default_speed = is_touchpad ? -0.55 : -0.35;
	double pointer_speed = default_speed;
	int natural_scroll = -1;

	if (libinput_device_config_accel_is_available(libinput)) {
		uint32_t profiles = libinput_device_config_accel_get_profiles(libinput);
		enum libinput_config_accel_profile profile = LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;
		if (prefer_flat_profile && (profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT)) {
			profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
		} else if (profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE) {
			profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
		} else if (profiles & LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT) {
			profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
		}

		if (profile != LIBINPUT_CONFIG_ACCEL_PROFILE_NONE) {
			enum libinput_config_status profile_status =
				libinput_device_config_accel_set_profile(libinput, profile);
			log_libinput_status(name, "accel profile", profile_status);
		}

		enum libinput_config_status speed_status =
			libinput_device_config_accel_set_speed(libinput, pointer_speed);
		log_libinput_status(name, "accel speed", speed_status);
	}

	if (tap_finger_count > 0) {
		enum libinput_config_status tap_status =
			libinput_device_config_tap_set_enabled(libinput,
				tap_enabled ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
		log_libinput_status(name, "tap", tap_status);

		enum libinput_config_status tap_drag_status =
			libinput_device_config_tap_set_drag_enabled(libinput,
				tap_drag_enabled ? LIBINPUT_CONFIG_DRAG_ENABLED : LIBINPUT_CONFIG_DRAG_DISABLED);
		log_libinput_status(name, "tap drag", tap_drag_status);

		enum libinput_config_status tap_drag_lock_status =
			libinput_device_config_tap_set_drag_lock_enabled(libinput,
				(tap_drag_enabled && tap_drag_lock_enabled) ?
					LIBINPUT_CONFIG_DRAG_LOCK_ENABLED_TIMEOUT :
					LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
		log_libinput_status(name, "tap drag lock", tap_drag_lock_status);
	}

	if (natural_scroll >= 0 &&
			libinput_device_config_scroll_has_natural_scroll(libinput)) {
		enum libinput_config_status natural_scroll_status =
			libinput_device_config_scroll_set_natural_scroll_enabled(
				libinput, natural_scroll ? 1 : 0);
		log_libinput_status(name, "natural scroll", natural_scroll_status);
	}

	wlr_log(WLR_INFO,
		"libinput configured for %s: touchpad=%d speed=%.2f flat=%d tap=%d tap_drag=%d",
		name, is_touchpad, pointer_speed, prefer_flat_profile, tap_enabled, tap_drag_enabled);
}

static void keyboard_modifiers_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static void maybe_restore_last_minimized(struct flux_server *server, uint32_t time_msec) {
	struct flux_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->minimized && view->mapped) {
			view_begin_restore_animation(view, time_msec);
			return;
		}
	}
}

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct flux_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct flux_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	bool handled = false;
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms = NULL;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state,
		keycode, &syms);

	uint32_t mods = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	bool mod_down = (mods & server->keybind_mod_mask) != 0;
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && mod_down) {
		for (int i = 0; i < nsyms; i++) {
			if (syms[i] == XKB_KEY_Escape) {
				wl_display_terminate(server->display);
				handled = true;
				break;
			}
			if (syms[i] == XKB_KEY_Return || syms[i] == XKB_KEY_KP_Enter) {
				launch_app(server, default_launch_command());
				handled = true;
				break;
			}
			if (syms[i] == XKB_KEY_m) {
				maybe_restore_last_minimized(server, event->time_msec);
				handled = true;
				break;
			}
		}
	}

	if (!handled) {
		wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
			event->keycode, event->state);
	}
}

static void keyboard_destroy_notify(struct wl_listener *listener, void *data) {
	(void)data;
	struct flux_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void add_keyboard(struct flux_server *server, struct wlr_input_device *device) {
	struct flux_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);

	wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, 25, 600);

	keyboard->modifiers.notify = keyboard_modifiers_notify;
	wl_signal_add(&keyboard->wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_key_notify;
	wl_signal_add(&keyboard->wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_destroy_notify;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wl_list_insert(&server->keyboards, &keyboard->link);
	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
}

static void update_seat_caps(struct flux_server *server) {
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void maybe_enable_parallels_cursor_compat(struct flux_server *server,
		struct wlr_input_device *device) {
	if (device->type != WLR_INPUT_DEVICE_POINTER || !device->name) {
		return;
	}

	if (strstr(device->name, "Parallels Virtual Mouse") == NULL) {
		return;
	}

	if (!server->use_drawn_cursor) {
		server->use_drawn_cursor = true;
		wlr_cursor_unset_image(server->cursor);
		if (!server->cursor_tree) {
			create_cursor_pointer(server);
		}

		wlr_log(WLR_INFO,
			"Parallels pointer detected; enabling drawn-cursor compatibility");
	}
}

void new_input_notify(struct wl_listener *listener, void *data) {
	struct flux_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	bool pointer_like = false;

	wlr_log(WLR_INFO, "new input device: type=%d name=%s",
		device->type, device->name ? device->name : "(null)");

	if (wlr_input_device_is_libinput(device)) {
		struct libinput_device *libinput = wlr_libinput_get_device_handle(device);
		if (libinput) {
			configure_libinput_device(device, libinput);
		}
	}

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		add_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		maybe_enable_parallels_cursor_compat(server, device);
		wlr_cursor_attach_input_device(server->cursor, device);
		pointer_like = true;
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		wlr_cursor_attach_input_device(server->cursor, device);
		pointer_like = true;
		break;
	case WLR_INPUT_DEVICE_TABLET:
		wlr_cursor_attach_input_device(server->cursor, device);
		pointer_like = true;
		break;
	case WLR_INPUT_DEVICE_TABLET_PAD:
	case WLR_INPUT_DEVICE_SWITCH:
	default:
		break;
	}
	update_seat_caps(server);

	if (pointer_like) {
		apply_default_cursor(server);
	}
}
