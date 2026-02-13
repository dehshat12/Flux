#include "flux.h"

static FILE *g_log_file = NULL;
static char g_log_path[PATH_MAX] = {0};
static enum wlr_log_importance g_log_verbosity = WLR_INFO;

static const char *log_level_name(enum wlr_log_importance importance) {
	switch (importance) {
	case WLR_ERROR:
		return "ERROR";
	case WLR_INFO:
		return "INFO";
	case WLR_DEBUG:
		return "DEBUG";
	case WLR_SILENT:
	default:
		return "SILENT";
	}
}

static void timestamp_now(char out[32]) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm tm;
	localtime_r(&ts.tv_sec, &tm);
	strftime(out, 32, "%Y-%m-%d %H:%M:%S", &tm);
}

static void create_parent_dirs(const char *path) {
	if (!path || path[0] == '\0') {
		return;
	}

	char tmp[PATH_MAX];
	size_t len = strlen(path);
	if (len >= sizeof(tmp)) {
		return;
	}
	memcpy(tmp, path, len + 1);

	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
			*p = '/';
			return;
		}
		*p = '/';
	}
}

static const char *default_log_path(void) {
	const char *custom = getenv("FLUX_LOG_FILE");
	if (custom && custom[0] != '\0') {
		return custom;
	}

	static char xdg_path[PATH_MAX];
	const char *xdg_state_home = getenv("XDG_STATE_HOME");
	if (xdg_state_home && xdg_state_home[0] != '\0') {
		int n = snprintf(xdg_path, sizeof(xdg_path), "%s/flux/flux.log", xdg_state_home);
		if (n > 0 && (size_t)n < sizeof(xdg_path)) {
			return xdg_path;
		}
	}

	static char home_path[PATH_MAX];
	const char *home = getenv("HOME");
	if (home && home[0] != '\0') {
		int n = snprintf(home_path, sizeof(home_path), "%s/.local/state/flux/flux.log", home);
		if (n > 0 && (size_t)n < sizeof(home_path)) {
			return home_path;
		}
	}

	return "/tmp/flux.log";
}

void init_logging(void) {
	const char *level = getenv("FLUX_LOG_LEVEL");
	if (level && strcmp(level, "debug") == 0) {
		g_log_verbosity = WLR_DEBUG;
	} else if (level && strcmp(level, "error") == 0) {
		g_log_verbosity = WLR_ERROR;
	} else if (level && strcmp(level, "silent") == 0) {
		g_log_verbosity = WLR_SILENT;
	} else {
		g_log_verbosity = WLR_INFO;
	}

	const char *path = default_log_path();
	create_parent_dirs(path);
	g_log_file = fopen(path, "a");

	if (!g_log_file && strcmp(path, "/tmp/flux.log") != 0) {
		path = "/tmp/flux.log";
		g_log_file = fopen(path, "a");
	}

	if (g_log_file) {
		setvbuf(g_log_file, NULL, _IOLBF, 0);
		snprintf(g_log_path, sizeof(g_log_path), "%s", path);
	}
}

void close_logging(void) {
	if (g_log_file) {
		fflush(g_log_file);
		fclose(g_log_file);
		g_log_file = NULL;
	}

	if (g_log_path[0] != '\0') {
		fprintf(stderr, "flux log: %s\n", g_log_path);
	}
}

const char *flux_log_path(void) {
	return g_log_path;
}

void flux_log_callback(enum wlr_log_importance importance,
		const char *fmt, va_list args) {
	if (importance > g_log_verbosity) {
		return;
	}

	char ts[32];
	timestamp_now(ts);
	const char *level = log_level_name(importance);

	va_list stderr_args;
	va_copy(stderr_args, args);
	fprintf(stderr, "%s [%s] ", ts, level);
	vfprintf(stderr, fmt, stderr_args);
	fputc('\n', stderr);
	fflush(stderr);
	va_end(stderr_args);

	if (g_log_file) {
		va_list file_args;
		va_copy(file_args, args);
		fprintf(g_log_file, "%s [%s] ", ts, level);
		vfprintf(g_log_file, fmt, file_args);
		fputc('\n', g_log_file);
		fflush(g_log_file);
		va_end(file_args);
	}
}

int handle_terminate_signal(int signal_number, void *data) {
	struct flux_server *server = data;
	wlr_log(WLR_INFO, "received signal %d, terminating", signal_number);
	wl_display_terminate(server->display);
	return 0;
}

void setup_child_reaping(void) {
	struct sigaction sa = {0};
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_NOCLDWAIT | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);
}
