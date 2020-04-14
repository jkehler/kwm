#include "server.h"
#include "kwm.h"
#include <unistd.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "config.h"


void kwm_spawn_process(struct kwm_server *server, const arg *arg) {
	wlr_log(WLR_DEBUG, "exec: %s %s", ((char **)arg->v)[0], (char **)arg->v);
	if (fork() == 0) {
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

void kwm_exit(struct kwm_server *server, const arg *arg) {
	wlr_log(WLR_INFO, "Exiting kwm");
	wl_display_terminate(server->display);
}

void kwm_kill_view(struct kwm_server *server, const arg *arg) {
}

bool handle_keybinding(struct kwm_server *server, uint32_t modifiers, xkb_keysym_t keysym) {
	for (int i = 0; i < LENGTH(keybinds); i++) {
		if (modifiers == keybinds[i].modifiers && keysym == keybinds[i].keysym) {
			keybinds[i].func(server, &(keybinds[i].arg));
			return true;
		}
	}
	return false;
}
	/* modifiers = modifiers & ~WLR_MODIFIER_ALT; */

	/* wlr_log(WLR_DEBUG, "modifiers: %d", modifiers); */
	/* switch (sym) { */
	/* case XKB_KEY_Escape: */
	/* 	wl_display_terminate(server->display); */
	/* 	break; */
	/* case XKB_KEY_Return: */
	/* 	if (fork() == 0) { */
	/* 		execl("/bin/sh", "/bin/sh", "-c", "/usr/bin/alacritty", (void *)NULL); */
	/* 	} */
	/* 	break; */
	/* case XKB_KEY_F1: */
	/* 	/1* Cycle to the next view *1/ */
	/* 	/1* if (wl_list_length(&server->views) < 2) { *1/ */
	/* 	/1* 	break; *1/ */
	/* 	/1* } *1/ */
	/* 	/1* struct tinywl_view *current_view = wl_container_of( *1/ */
	/* 	/1* 	server->views.next, current_view, link); *1/ */
	/* 	/1* struct tinywl_view *next_view = wl_container_of( *1/ */
	/* 	/1* 	current_view->link.next, next_view, link); *1/ */
	/* 	/1* focus_view(next_view, next_view->xdg_surface->surface); *1/ */
	/* 	/1* /2* Move the previous view to the end of the list *2/ *1/ */
	/* 	/1* wl_list_remove(&current_view->link); *1/ */
	/* 	/1* wl_list_insert(server->views.prev, &current_view->link); *1/ */
	/* 	break; */
	/* default: */
	/* 	return false; */
	/* } */
	/* return true; */

int main(int argc, char *argv[]) {
	/* Set our log level */
	wlr_log_init(WLR_DEBUG, NULL);

	struct kwm_server server;

	if (!server_init(&server)) {
		wlr_log(WLR_ERROR, "Failed to initialize the Wayland server");
		exit(EXIT_FAILURE);
	}
	setenv("WAYLAND_DISPLAY", server.socket, true);

	if (!server_start(&server)) {
		goto shutdown;
	}

	server_run(&server);

shutdown:
	server_cleanup(&server);
	return 0;
}
