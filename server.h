#ifndef KWM_SERVER_H
#define KWM_SERVER_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_pointer.h>

enum kwm_cursor_mode {
	KWM_CURSOR_PASSTHROUGH,
	KWM_CURSOR_MOVE,
	KWM_CURSOR_RESIZE
};

/* This is the main kwm server struct */
struct kwm_server {
	struct wl_display 						*display;
	struct wlr_backend						*backend;
	struct wlr_renderer						*renderer;
	struct wlr_compositor					*compositor;
	struct wlr_output_layout				*output_layout;
	struct wlr_xdg_shell					*xdg_shell;
	struct wlr_cursor						*cursor;
	struct wlr_xcursor_manager				*cursor_mgr;
	struct wlr_seat							*seat;
	struct wlr_server_decoration_manager	*decoration_mgr;
	struct wlr_xdg_decoration_manager_v1	*xdg_decoration_mgr;
	struct kwm_view							*grabbed_view;
	double 									grab_x, grab_y;
	int										grab_width, grab_height;
	uint32_t								resize_edges;
	enum kwm_cursor_mode					cursor_mode;

	struct wl_list				outputs;
	struct wl_list				views;
	struct wl_list				keyboards;

	struct wl_listener			new_xdg_surface;
	struct wl_listener			new_xdg_decoration;
	struct wl_listener			new_output;
	struct wl_listener			cursor_motion;
	struct wl_listener			cursor_motion_abs;
	struct wl_listener			cursor_axis;
	struct wl_listener			cursor_frame;
	struct wl_listener			cursor_button;
	struct wl_listener			request_set_cursor;
	struct wl_listener			new_input;
};

/* This struct holds the state for the connected outputs (displays) */
struct kwm_output {
	struct wlr_output	*output;
	struct kwm_server	*server;
	struct timespec		last_frame;

	struct wl_list		views;
	struct wl_list		link;
	struct wl_listener	destroy;
	struct wl_listener	frame;
};

/* This struct holds the state of a view (application) */
struct kwm_view {
	struct kwm_server						*server;
	struct kwm_output						*output;
	struct wl_list							link;
	struct wlr_xdg_surface					*xdg_surface;
	struct wlr_xdg_toplevel_decoration_v1	*xdg_decoration;
	bool 									mapped;
	int 									x, y;

	struct wl_listener		map;
	struct wl_listener		unmap;
	struct wl_listener		destroy;
	struct wl_listener		request_move;
	struct wl_listener		request_resize;
};

/* This struct holds the state of a keyboard */
struct kwm_keyboard {
	struct wl_list 			link;
	struct kwm_server 		*server;
	struct wlr_input_device	*device;

	struct wl_listener		modifiers;
	struct wl_listener		key;
};

struct render_data {
	struct wlr_output		*output;
	struct wlr_renderer		*renderer;
	struct kwm_view			*view;
	struct timespec			*when;
};

bool server_init(struct kwm_server *server);
int server_start(struct kwm_server *server, char *startup_cmd);

void render_surface(struct wlr_surface *surface, int sx, int sy, void *data);
void focus_view(struct kwm_view *view, struct wlr_surface *surface);
void process_cursor_motion(struct kwm_server *server, uint32_t time);
void process_cursor_move(struct kwm_server *server, uint32_t time);
void process_cursor_resize(struct kwm_server *server, uint32_t time);

void handle_output_frame(struct wl_listener *listener, void *data);
void handle_new_output(struct wl_listener *listener, void *data);
void handle_output_destroy(struct wl_listener *listener, void *data);
void handle_new_xdg_surface(struct wl_listener *listener, void *data);
void handle_xdg_surface_map(struct wl_listener *listener, void *data);
void handle_xdg_surface_unmap(struct wl_listener *listener, void *data);
void handle_xdg_surface_destroy(struct wl_listener *listener, void *data);
void handle_xdg_decoration(struct wl_listener *listener, void *data);
void handle_xdg_toplevel_request_move(struct wl_listener *listener, void *data);
void handle_xdg_toplevel_request_resize(struct wl_listener *listener, void *data);
void handle_cursor_motion(struct wl_listener *listener, void *data);
void handle_cursor_motion_abs(struct wl_listener *listener, void *data);
void handle_cursor_button(struct wl_listener *listener, void *data);
void handle_cursor_axis(struct wl_listener *listener, void *data);
void handle_cursor_frame(struct wl_listener *listener, void *data);
void handle_new_input(struct wl_listener *listener, void *data);
void handle_input_destroy(struct wl_listener *listener, void *data);
void handle_keyboard_modifiers(struct wl_listener *listener, void *data);
void handle_keyboard_key(struct wl_listener *listener, void *data);

void add_new_pointer(struct kwm_server *server, struct wlr_input_device *device);
void add_new_keyboard(struct kwm_server *server, struct wlr_input_device *device);

void begin_interactive(struct kwm_view *view, enum kwm_cursor_mode mode, uint32_t edges);
#endif
