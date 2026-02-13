#include "flux.h"

#include <ctype.h>

#define TASKBAR_HEIGHT 30
#define TASKBAR_MARGIN 6
#define TASKBAR_BUTTON_H 22
#define TASKBAR_BUTTON_MIN_W 110
#define TASKBAR_BUTTON_MAX_W 240
#define TASKBAR_TEXT_PAD_X 8
#define TASKBAR_TEXT_SCALE 1
#define TASKBAR_GLYPH_W 5
#define TASKBAR_GLYPH_H 7
#define TASKBAR_TEXT_ADV ((TASKBAR_GLYPH_W + 1) * TASKBAR_TEXT_SCALE)
#define TASKBAR_TEXT_HEIGHT (TASKBAR_GLYPH_H * TASKBAR_TEXT_SCALE)

static const float COLOR_WIN98_TASKBAR_BG[4] = {0.7529f, 0.7529f, 0.7529f, 1.0f};
static const float COLOR_WIN98_FACE[4] = {0.7529f, 0.7529f, 0.7529f, 1.0f};
static const float COLOR_WIN98_HILIGHT[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static const float COLOR_WIN98_LIGHT[4] = {0.8784f, 0.8784f, 0.8784f, 1.0f};
static const float COLOR_WIN98_SHADOW[4] = {0.5020f, 0.5020f, 0.5020f, 1.0f};
static const float COLOR_WIN98_DARK[4] = {0.0f, 0.0f, 0.0f, 1.0f};
static const float COLOR_WIN98_FACE_PRESSED[4] = {0.6902f, 0.6902f, 0.6902f, 1.0f};
static const float COLOR_WIN98_TEXT[4] = {0.0f, 0.0f, 0.0f, 1.0f};

struct glyph_5x7 {
	char ch;
	uint8_t rows[TASKBAR_GLYPH_H];
};

static const struct glyph_5x7 GLYPHS[] = {
	{'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
	{'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
	{'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
	{'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
	{'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
	{'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
	{'G', {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E}},
	{'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
	{'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
	{'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}},
	{'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
	{'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
	{'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
	{'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
	{'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
	{'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
	{'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
	{'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
	{'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
	{'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
	{'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
	{'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
	{'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
	{'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
	{'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
	{'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
	{'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
	{'1', {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}},
	{'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
	{'3', {0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E}},
	{'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
	{'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
	{'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
	{'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
	{'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
	{'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
	{'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
	{'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
	{'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04}},
	{':', {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}},
	{'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
	{'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
	{')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
	{'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
};

static const uint8_t EMPTY_GLYPH[TASKBAR_GLYPH_H] = {0, 0, 0, 0, 0, 0, 0};
static const uint8_t UNKNOWN_GLYPH[TASKBAR_GLYPH_H] = {0x1F, 0x11, 0x01, 0x06, 0x04, 0x00, 0x04};

static int taskbar_bar_height(void) {
	int h = TASKBAR_HEIGHT;
	if (h < TASKBAR_BUTTON_H + 4) {
		h = TASKBAR_BUTTON_H + 4;
	}
	return h;
}

static const char *view_display_title(const struct flux_view *view) {
	if (view->xdg_surface && view->xdg_surface->toplevel) {
		const char *title = view->xdg_surface->toplevel->title;
		if (title && title[0] != '\0') {
			return title;
		}
		const char *app_id = view->xdg_surface->toplevel->app_id;
		if (app_id && app_id[0] != '\0') {
			return app_id;
		}
	}
	return "APP";
}

static const uint8_t *glyph_rows_for_char(char ch) {
	if (ch == ' ') {
		return EMPTY_GLYPH;
	}

	unsigned char uch = (unsigned char)ch;
	ch = (char)toupper(uch);
	for (size_t i = 0; i < sizeof(GLYPHS) / sizeof(GLYPHS[0]); i++) {
		if (GLYPHS[i].ch == ch) {
			return GLYPHS[i].rows;
		}
	}
	return UNKNOWN_GLYPH;
}

static void draw_scaled_run(struct wlr_scene_tree *parent,
		int x, int y, int width_px, int scale, const float color[4]) {
	if (width_px <= 0 || scale <= 0) {
		return;
	}
	struct wlr_scene_rect *r =
		wlr_scene_rect_create(parent, width_px * scale, scale, color);
	wlr_scene_node_set_position(&r->node, x, y);
}

static void draw_glyph(struct wlr_scene_tree *parent, int x, int y,
		char ch, int scale, const float color[4]) {
	const uint8_t *rows = glyph_rows_for_char(ch);
	for (int row = 0; row < TASKBAR_GLYPH_H; row++) {
		int run_start = -1;
		for (int col = 0; col < TASKBAR_GLYPH_W; col++) {
			bool on = (rows[row] & (1u << (TASKBAR_GLYPH_W - 1 - col))) != 0;
			if (on && run_start < 0) {
				run_start = col;
			}
			bool flush = (!on || col == TASKBAR_GLYPH_W - 1) && run_start >= 0;
			if (flush) {
				int run_end = on && col == TASKBAR_GLYPH_W - 1 ? col : col - 1;
				draw_scaled_run(parent,
					x + run_start * scale,
					y + row * scale,
					run_end - run_start + 1,
					scale,
					color);
				run_start = -1;
			}
		}
	}
}

static struct wlr_scene_rect *create_rect(struct wlr_scene_tree *parent,
		int x, int y, int w, int h, const float color[4]) {
	if (w <= 0 || h <= 0) {
		return NULL;
	}
	struct wlr_scene_rect *rect = wlr_scene_rect_create(parent, w, h, color);
	wlr_scene_node_set_position(&rect->node, x, y);
	return rect;
}

static void draw_win98_button(struct wlr_scene_tree *button_tree,
		int button_w, int button_h, bool pressed) {
	if (button_w <= 2 || button_h <= 2) {
		wlr_scene_rect_create(button_tree, button_w, button_h, COLOR_WIN98_FACE);
		return;
	}

	if (pressed) {
		/* Sunken button: dark top/left and light bottom/right. */
		create_rect(button_tree, 1, 1, button_w - 2, button_h - 2, COLOR_WIN98_FACE_PRESSED);
		create_rect(button_tree, 0, 0, button_w - 1, 1, COLOR_WIN98_DARK);
		create_rect(button_tree, 0, 0, 1, button_h - 1, COLOR_WIN98_DARK);
		create_rect(button_tree, 1, 1, button_w - 3, 1, COLOR_WIN98_SHADOW);
		create_rect(button_tree, 1, 1, 1, button_h - 3, COLOR_WIN98_SHADOW);
		create_rect(button_tree, button_w - 2, 1, 1, button_h - 3, COLOR_WIN98_HILIGHT);
		create_rect(button_tree, 1, button_h - 2, button_w - 3, 1, COLOR_WIN98_HILIGHT);
		create_rect(button_tree, button_w - 1, 0, 1, button_h, COLOR_WIN98_LIGHT);
		create_rect(button_tree, 0, button_h - 1, button_w, 1, COLOR_WIN98_LIGHT);
		return;
	}

	/* Raised button: light top/left, dark right/bottom bevel. */
	create_rect(button_tree, 1, 1, button_w - 2, button_h - 2, COLOR_WIN98_FACE);
	create_rect(button_tree, 0, 0, button_w - 1, 1, COLOR_WIN98_HILIGHT);
	create_rect(button_tree, 0, 0, 1, button_h - 1, COLOR_WIN98_HILIGHT);
	create_rect(button_tree, 1, 1, button_w - 3, 1, COLOR_WIN98_LIGHT);
	create_rect(button_tree, 1, 1, 1, button_h - 3, COLOR_WIN98_LIGHT);
	create_rect(button_tree, button_w - 2, 1, 1, button_h - 3, COLOR_WIN98_SHADOW);
	create_rect(button_tree, 1, button_h - 2, button_w - 3, 1, COLOR_WIN98_SHADOW);
	create_rect(button_tree, button_w - 1, 0, 1, button_h, COLOR_WIN98_DARK);
	create_rect(button_tree, 0, button_h - 1, button_w, 1, COLOR_WIN98_DARK);
}

static int text_pixel_width(const char *text, int nchars) {
	if (!text || nchars <= 0) {
		return 0;
	}
	return nchars * TASKBAR_TEXT_ADV - TASKBAR_TEXT_SCALE;
}

static int taskbar_button_width_for_title(const char *title) {
	int title_px = text_pixel_width(title, (int)strlen(title));
	int button_w = title_px + TASKBAR_TEXT_PAD_X * 2;
	if (button_w < TASKBAR_BUTTON_MIN_W) {
		button_w = TASKBAR_BUTTON_MIN_W;
	}
	if (button_w > TASKBAR_BUTTON_MAX_W) {
		button_w = TASKBAR_BUTTON_MAX_W;
	}
	return button_w;
}

static void draw_button_label(struct wlr_scene_tree *button_tree,
		const char *title, int button_w, int button_h, bool pressed) {
	int usable_w = button_w - TASKBAR_TEXT_PAD_X * 2;
	if (usable_w <= 0) {
		return;
	}

	int max_chars = usable_w / TASKBAR_TEXT_ADV;
	if (max_chars <= 0) {
		return;
	}

	char label[128];
	size_t title_len = strlen(title);
	if ((int)title_len <= max_chars) {
		snprintf(label, sizeof(label), "%s", title);
	} else if (max_chars >= 3) {
		int keep = max_chars - 3;
		if (keep > (int)sizeof(label) - 4) {
			keep = (int)sizeof(label) - 4;
		}
		snprintf(label, sizeof(label), "%.*s...", keep, title);
	} else {
		snprintf(label, sizeof(label), "%.*s", max_chars, title);
	}

	int label_len = (int)strlen(label);
	int label_w = text_pixel_width(label, label_len);
	int text_x = TASKBAR_TEXT_PAD_X;
	if (label_w < usable_w) {
		text_x += (usable_w - label_w) / 2;
	}
	if (pressed) {
		text_x += 1;
	}
	int text_y = (button_h - TASKBAR_TEXT_HEIGHT) / 2;
	if (text_y < 0) {
		text_y = 0;
	}
	if (pressed) {
		text_y += 1;
	}

	for (int i = 0; i < label_len; i++) {
		draw_glyph(button_tree,
			text_x + i * TASKBAR_TEXT_ADV,
			text_y,
			label[i],
			TASKBAR_TEXT_SCALE,
			COLOR_WIN98_TEXT);
	}
}

static void clear_taskbar_view_state(struct flux_server *server) {
	struct flux_view *view;
	wl_list_for_each(view, &server->views, link) {
		view->taskbar_visible = false;
		view->taskbar_x = 0;
		view->taskbar_y = 0;
		view->taskbar_width = 0;
		view->taskbar_height = 0;
	}
}

void taskbar_init(struct flux_server *server) {
	server->taskbar_tree = wlr_scene_tree_create(&server->scene->tree);
	server->taskbar_bg_rect =
		wlr_scene_rect_create(server->taskbar_tree, 1, 1, COLOR_TASKBAR_BG);
	server->taskbar_buttons_tree = wlr_scene_tree_create(server->taskbar_tree);
	server->taskbar_layout_x = 0;
	server->taskbar_layout_y = 0;
	server->taskbar_layout_width = -1;
	server->taskbar_layout_height = -1;
	server->taskbar_dirty = true;
	wlr_scene_node_set_enabled(&server->taskbar_tree->node, false);
}

void taskbar_mark_dirty(struct flux_server *server) {
	server->taskbar_dirty = true;
	struct flux_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

struct flux_view *taskbar_view_at(struct flux_server *server, double lx, double ly) {
	struct flux_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->taskbar_visible || !view->mapped || !view->minimized) {
			continue;
		}
		if (lx >= view->taskbar_x &&
				ly >= view->taskbar_y &&
				lx < view->taskbar_x + view->taskbar_width &&
				ly < view->taskbar_y + view->taskbar_height) {
			return view;
		}
	}
	return NULL;
}

bool taskbar_predict_button_box(struct flux_server *server, struct flux_view *target,
		bool include_target_if_not_minimized, struct wlr_box *out) {
	if (!server || !target || !out) {
		return false;
	}

	struct wlr_box box = {0};
	wlr_output_layout_get_box(server->output_layout, NULL, &box);
	if (box.width <= 0 || box.height <= 0) {
		return false;
	}

	int bar_h = taskbar_bar_height();
	int button_h = TASKBAR_BUTTON_H;
	if (button_h > bar_h - 4) {
		button_h = bar_h - 4;
	}
	if (button_h < 10) {
		button_h = bar_h;
	}

	int button_y = (bar_h - button_h) / 2;
	int bar_y = box.y + box.height - bar_h;
	int cursor_x = TASKBAR_MARGIN;

	struct flux_view *view;
	wl_list_for_each(view, &server->views, link) {
		bool in_taskbar = view->mapped && (view->minimized ||
			(include_target_if_not_minimized && view == target && !view->minimized));
		if (!in_taskbar) {
			continue;
		}

		const char *title = view_display_title(view);
		int button_w = taskbar_button_width_for_title(title);
		int remaining = box.width - TASKBAR_MARGIN - cursor_x;
		if (remaining < TASKBAR_BUTTON_MIN_W) {
			if (view == target) {
				return false;
			}
			continue;
		}
		if (button_w > remaining) {
			button_w = remaining;
		}

		if (view == target) {
			out->x = box.x + cursor_x;
			out->y = bar_y + button_y;
			out->width = button_w;
			out->height = button_h;
			return true;
		}

		cursor_x += button_w + TASKBAR_MARGIN;
	}

	return false;
}

void taskbar_update(struct flux_server *server) {
	if (!server->taskbar_tree || !server->taskbar_bg_rect) {
		return;
	}

	struct wlr_box box = {0};
	wlr_output_layout_get_box(server->output_layout, NULL, &box);
	if (box.width <= 0 || box.height <= 0) {
		clear_taskbar_view_state(server);
		wlr_scene_node_set_enabled(&server->taskbar_tree->node, false);
		return;
	}

	int bar_h = taskbar_bar_height();
	bool layout_changed = box.x != server->taskbar_layout_x ||
		box.y != server->taskbar_layout_y ||
		box.width != server->taskbar_layout_width ||
		box.height != server->taskbar_layout_height;
	if (layout_changed) {
		server->taskbar_layout_x = box.x;
		server->taskbar_layout_y = box.y;
		server->taskbar_layout_width = box.width;
		server->taskbar_layout_height = box.height;
		server->taskbar_dirty = true;
	}

	if (!server->taskbar_dirty) {
		return;
	}

	clear_taskbar_view_state(server);
	if (server->taskbar_buttons_tree) {
		wlr_scene_node_destroy(&server->taskbar_buttons_tree->node);
	}
	server->taskbar_buttons_tree = wlr_scene_tree_create(server->taskbar_tree);

	int bar_y = box.y + box.height - bar_h;
	wlr_scene_node_set_position(&server->taskbar_tree->node, box.x, bar_y);

	wlr_scene_rect_set_size(server->taskbar_bg_rect, box.width, bar_h);
	wlr_scene_rect_set_color(server->taskbar_bg_rect, COLOR_WIN98_TASKBAR_BG);
	wlr_scene_node_set_enabled(&server->taskbar_bg_rect->node, true);

	int button_h = TASKBAR_BUTTON_H;
	if (button_h > bar_h - 4) {
		button_h = bar_h - 4;
	}
	if (button_h < 10) {
		button_h = bar_h;
	}
	int button_y = (bar_h - button_h) / 2;
	int cursor_x = TASKBAR_MARGIN;
	int shown = 0;

	struct flux_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped || !view->minimized) {
			continue;
		}

		const char *title = view_display_title(view);
		int button_w = taskbar_button_width_for_title(title);

		int remaining = box.width - TASKBAR_MARGIN - cursor_x;
		if (remaining < TASKBAR_BUTTON_MIN_W) {
			break;
		}
		if (button_w > remaining) {
			button_w = remaining;
		}

		struct wlr_scene_tree *button_tree = wlr_scene_tree_create(server->taskbar_buttons_tree);
		wlr_scene_node_set_position(&button_tree->node, cursor_x, button_y);

		bool pressed = server->pressed_taskbar_view == view;
		draw_win98_button(button_tree, button_w, button_h, pressed);
		draw_button_label(button_tree, title, button_w, button_h, pressed);

		view->taskbar_visible = true;
		view->taskbar_x = box.x + cursor_x;
		view->taskbar_y = bar_y + button_y;
		view->taskbar_width = button_w;
		view->taskbar_height = button_h;

		cursor_x += button_w + TASKBAR_MARGIN;
		shown++;
	}

	wlr_scene_node_set_enabled(&server->taskbar_tree->node, shown > 0);
	if (shown > 0) {
		wlr_scene_node_raise_to_top(&server->taskbar_tree->node);
		if (server->cursor_tree) {
			wlr_scene_node_raise_to_top(&server->cursor_tree->node);
		}
	}
	server->taskbar_dirty = false;
}
