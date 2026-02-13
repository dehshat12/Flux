#include "flux.h"

const char *default_launch_command(void) {
	const char *cmd = getenv("FLUX_LAUNCH_CMD");
	if (cmd && cmd[0] != '\0') {
		return cmd;
	}
	return "foot || xterm";
}

void launch_app(struct flux_server *server, const char *command) {
	if (!command || command[0] == '\0') {
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "failed to fork for launch command");
		return;
	}

	if (pid == 0) {
		setsid();
		execl("/bin/sh", "sh", "-lc", command, (char *)NULL);
		_exit(127);
	}

	wlr_log(WLR_INFO, "launched app pid=%d cmd=%s", (int)pid, command);
	(void)server;
}
