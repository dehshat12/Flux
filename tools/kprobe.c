#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

static const char *connector_status_name(drmModeConnection status) {
	switch (status) {
	case DRM_MODE_CONNECTED:
		return "connected";
	case DRM_MODE_DISCONNECTED:
		return "disconnected";
	case DRM_MODE_UNKNOWNCONNECTION:
	default:
		return "unknown";
	}
}

static const char *plane_type_name(uint64_t type) {
	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		return "primary";
	case DRM_PLANE_TYPE_CURSOR:
		return "cursor";
	case DRM_PLANE_TYPE_OVERLAY:
		return "overlay";
	default:
		return "unknown";
	}
}

static void print_cap(int fd, uint64_t cap, const char *name) {
	uint64_t value = 0;
	if (drmGetCap(fd, cap, &value) == 0) {
		printf("cap %-24s : %" PRIu64 "\n", name, value);
	} else {
		printf("cap %-24s : unavailable (%s)\n", name, strerror(errno));
	}
}

static uint64_t get_plane_type(int fd, uint32_t plane_id) {
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
	if (!props) {
		return 0;
	}

	uint64_t plane_type = 0;
	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop) {
			continue;
		}

		if (strcmp(prop->name, "type") == 0) {
			plane_type = props->prop_values[i];
			drmModeFreeProperty(prop);
			break;
		}

		drmModeFreeProperty(prop);
	}

	drmModeFreeObjectProperties(props);
	return plane_type;
}

static void print_driver_info(int fd) {
	drmVersion *version = drmGetVersion(fd);
	if (!version) {
		printf("driver: unavailable\n");
		return;
	}

	printf("driver: %.*s %d.%d.%d\n",
		version->name_len, version->name,
		version->version_major, version->version_minor, version->version_patchlevel);
	printf("desc  : %.*s\n", version->desc_len, version->desc);
	printf("date  : %.*s\n", version->date_len, version->date);
	drmFreeVersion(version);
}

static void print_connectors(int fd, drmModeRes *res) {
	printf("\nconnectors (%d):\n", res->count_connectors);
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn) {
			printf("  connector %u: failed to query\n", res->connectors[i]);
			continue;
		}

		printf("  id=%u type=%u type_id=%u status=%s modes=%d encoders=%d mm=%ux%u\n",
			conn->connector_id,
			conn->connector_type,
			conn->connector_type_id,
			connector_status_name(conn->connection),
			conn->count_modes,
			conn->count_encoders,
			conn->mmWidth,
			conn->mmHeight);

		if (conn->count_modes > 0) {
			int max_modes = conn->count_modes < 5 ? conn->count_modes : 5;
			for (int m = 0; m < max_modes; m++) {
				drmModeModeInfo *mode = &conn->modes[m];
				printf("    mode[%d]=%s %dx%d@%d\n",
					m,
					mode->name,
					mode->hdisplay,
					mode->vdisplay,
					mode->vrefresh);
			}
			if (conn->count_modes > max_modes) {
				printf("    ... (%d more)\n", conn->count_modes - max_modes);
			}
		}

		drmModeFreeConnector(conn);
	}
}

static void print_crtcs(int fd, drmModeRes *res) {
	printf("\ncrtcs (%d):\n", res->count_crtcs);
	for (int i = 0; i < res->count_crtcs; i++) {
		drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);
		if (!crtc) {
			printf("  crtc %u: failed to query\n", res->crtcs[i]);
			continue;
		}

		printf("  id=%u buffer=%u x=%u y=%u mode_valid=%d gamma=%d\n",
			crtc->crtc_id,
			crtc->buffer_id,
			crtc->x,
			crtc->y,
			crtc->mode_valid,
			crtc->gamma_size);

		if (crtc->mode_valid) {
			printf("    mode=%s %dx%d@%d\n",
				crtc->mode.name,
				crtc->mode.hdisplay,
				crtc->mode.vdisplay,
				crtc->mode.vrefresh);
		}

		drmModeFreeCrtc(crtc);
	}
}

static void print_planes(int fd, drmModeRes *res) {
	drmModePlaneRes *plane_res = drmModeGetPlaneResources(fd);
	if (!plane_res) {
		printf("\nplanes: unavailable (%s)\n", strerror(errno));
		return;
	}

	printf("\nplanes (%u):\n", plane_res->count_planes);
	for (uint32_t i = 0; i < plane_res->count_planes; i++) {
		drmModePlane *plane = drmModeGetPlane(fd, plane_res->planes[i]);
		if (!plane) {
			printf("  plane %u: failed to query\n", plane_res->planes[i]);
			continue;
		}

		uint64_t plane_type = get_plane_type(fd, plane->plane_id);
		printf("  id=%u type=%s possible_crtcs=0x%x formats=%u",
			plane->plane_id,
			plane_type_name(plane_type),
			plane->possible_crtcs,
			plane->count_formats);

		if (plane_type == DRM_PLANE_TYPE_CURSOR) {
			printf("  <-- cursor plane\n");
		} else {
			printf("\n");
		}

		if (plane->count_formats > 0) {
			uint32_t fmt = plane->formats[0];
			char fourcc[5] = {
				(char)(fmt & 0xff),
				(char)((fmt >> 8) & 0xff),
				(char)((fmt >> 16) & 0xff),
				(char)((fmt >> 24) & 0xff),
				'\0',
			};
			printf("    first format: 0x%08x (%s)\n", fmt, fourcc);
		}

		drmModeFreePlane(plane);
	}

	drmModeFreePlaneResources(plane_res);

	printf("\ncrtc index map:\n");
	for (int i = 0; i < res->count_crtcs; i++) {
		printf("  index=%d crtc_id=%u\n", i, res->crtcs[i]);
	}
}

int main(int argc, char **argv) {
	const char *card_path = "/dev/dri/card0";
	if (argc > 1) {
		card_path = argv[1];
	}

	int fd = open(card_path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s: %s\n", card_path, strerror(errno));
		return 1;
	}

	printf("kprobe: %s\n", card_path);
	print_driver_info(fd);

	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
		printf("warn: could not enable UNIVERSAL_PLANES (%s)\n", strerror(errno));
	}
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
		printf("warn: could not enable ATOMIC (%s)\n", strerror(errno));
	}

	printf("\ncapabilities:\n");
	print_cap(fd, DRM_CAP_DUMB_BUFFER, "DUMB_BUFFER");
	print_cap(fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, "CRTC_IN_VBLANK_EVENT");
	print_cap(fd, DRM_CAP_CURSOR_WIDTH, "CURSOR_WIDTH");
	print_cap(fd, DRM_CAP_CURSOR_HEIGHT, "CURSOR_HEIGHT");
#ifdef DRM_CAP_UNIVERSAL_PLANES
	print_cap(fd, DRM_CAP_UNIVERSAL_PLANES, "UNIVERSAL_PLANES");
#else
	printf("cap %-24s : unavailable (macro not defined)\n", "UNIVERSAL_PLANES");
#endif
#ifdef DRM_CAP_ATOMIC
	print_cap(fd, DRM_CAP_ATOMIC, "ATOMIC");
#else
	printf("cap %-24s : unavailable (macro not defined)\n", "ATOMIC");
#endif

	drmModeRes *res = drmModeGetResources(fd);
	if (!res) {
		fprintf(stderr, "failed to get DRM resources: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	printf("\nresource limits:\n");
	printf("  min %dx%d\n", res->min_width, res->min_height);
	printf("  max %dx%d\n", res->max_width, res->max_height);

	print_connectors(fd, res);
	print_crtcs(fd, res);
	print_planes(fd, res);

	drmModeFreeResources(res);
	close(fd);
	return 0;
}
