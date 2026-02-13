#include "flux.h"

int env_int(const char *name, int fallback) {
	const char *val = getenv(name);
	if (!val || val[0] == '\0') {
		return fallback;
	}

	char *end = NULL;
	long parsed = strtol(val, &end, 10);
	if (end == val || *end != '\0') {
		return fallback;
	}

	if (parsed > INT_MAX) {
		return INT_MAX;
	}
	if (parsed < INT_MIN) {
		return INT_MIN;
	}
	return (int)parsed;
}

uint32_t parse_keybind_mod_mask(void) {
	const char *mod = getenv("FLUX_BIND_MOD");
	if (!mod || mod[0] == '\0') {
		return WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO;
	}

	if (strcmp(mod, "alt") == 0 || strcmp(mod, "option") == 0) {
		return WLR_MODIFIER_ALT;
	}
	if (strcmp(mod, "super") == 0 || strcmp(mod, "logo") == 0 ||
			strcmp(mod, "cmd") == 0 || strcmp(mod, "command") == 0) {
		return WLR_MODIFIER_LOGO;
	}
	if (strcmp(mod, "ctrl") == 0 || strcmp(mod, "control") == 0) {
		return WLR_MODIFIER_CTRL;
	}
	if (strcmp(mod, "alt+super") == 0 || strcmp(mod, "super+alt") == 0 ||
			strcmp(mod, "alt_or_super") == 0) {
		return WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO;
	}

	// Safe default for mixed desktop/VM setups.
	return WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO;
}
