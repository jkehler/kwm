#include "server.h"
#include "kwm.h"
#include <stdlib.h>
#include <unistd.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>

/* XDG toplevels may have nested surfaces, such as popup windows for context menus
   or tooltips. This function tests if any of those are underneath the coordinates
   lx and ly (in output Layout coordinates). If so, it sets the surface pointer to
   that wlr_surface and the sx and sy coordinates to the coordinates relative to
   that surface's top-left corner. */
bool view_at(struct kwm_view *view, double lx, double ly, struct wlr_surface **surface, double *sx,
			 double *sy) {
	double view_sx = lx - view->x;
	double view_sy = ly - view->y;

	struct wlr_surface_state *state = &view->xdg_surface->surface->current;

	double _sx, _sy;
	struct wlr_surface *_surface = NULL;
	_surface = wlr_xdg_surface_surface_at(view->xdg_surface, view_sx, view_sy, &_sx, &_sy);

	if (_surface != NULL) {
		*sx = _sx;
		*sy = _sy;
		*surface = _surface;
		return true;
	}
	return false;
}

/* This iterates over all of the surfaces and tries to find one under the cursor */
struct kwm_view *desktop_view_at(struct kwm_server *server, double lx, double ly,
								 struct wlr_surface **surface, double *sx, double *sy) {
	struct kwm_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view_at(view, lx, ly, surface, sx, sy)) {
			return view;
		}
	}
	return NULL;
}

/* This function sets the focus on a view */
void focus_view(struct kwm_view *view, struct wlr_surface *surface) {
	/* Note: this function only deals with keyboard focus */
	if (view == NULL) {
		return;
	}
	wlr_log(WLR_DEBUG, "focus_view x=%d y=%d", view->x, view->y);
	struct kwm_server *server = view->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	if (prev_surface) {
		/* Deactivate the previously focused surface. This lets the client know
		   it no longer has focus and the client will repaint accordingly */
		struct wlr_xdg_surface *previous =
			wlr_xdg_surface_from_wlr_surface(seat->keyboard_state.focused_surface);
		wlr_xdg_toplevel_set_activated(previous, false);
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);

	/* move the view to the front */
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);

	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(view->xdg_surface, true);

	/* Tell the seat to have the keyboard enter this surface */
	wlr_seat_keyboard_notify_enter(seat, view->xdg_surface->surface, keyboard->keycodes,
								   keyboard->num_keycodes, &keyboard->modifiers);
}

/* This function renders a view */
void render_view(struct kwm_view *view, struct kwm_output *output, struct timespec now) {
	if (!view->mapped) {
		/* An unmapped view should not be rendered */
		return;
	}

	/* The view has a position in layout coordinates. If you have two displays,
	   one next to the other, both 1080p, a view on the right-most display might
	   have layout coordinates of 2000,100. We need to translate that to
	   output-local coordinates, or (2000-1928) */
	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(view->server->output_layout, output->output, &ox, &ox);
	ox += view->x, oy += view->y;

	/* Render the border for the currently focused view */
	if (view->xdg_surface->toplevel->current.activated) {
		int border_width = 2;
		float border_color[4] = {1.0, 0.3, 0.3, 1.0};

		struct wlr_box border_top = {
			.x = ox * output->output->scale,
			.y = (oy - border_width) * output->output->scale,
			.width =
				(border_width + view->xdg_surface->surface->current.width) * output->output->scale,
			.height = border_width * output->output->scale};
		wlr_render_rect(view->server->renderer, &border_top, border_color,
						output->output->transform_matrix);
		struct wlr_box border_bottom = {
			.x = ox * output->output->scale,
			.y = (oy + view->xdg_surface->surface->current.height) * output->output->scale,
			.width =
				(border_width + view->xdg_surface->surface->current.width) * output->output->scale,
			.height = border_width * output->output->scale};
		wlr_render_rect(view->server->renderer, &border_bottom, border_color,
						output->output->transform_matrix);
		struct wlr_box border_left = {
			.x = (ox - border_width) * output->output->scale,
			.y = (oy - border_width) * output->output->scale,
			.width = border_width * output->output->scale,
			.height = (border_width * 2 + view->xdg_surface->surface->current.height) *
					  output->output->scale};
		wlr_render_rect(view->server->renderer, &border_left, border_color,
						output->output->transform_matrix);
		struct wlr_box border_right = {
			.x = (ox + view->xdg_surface->surface->current.width) * output->output->scale,
			.y = oy * output->output->scale,
			.width = border_width * output->output->scale,
			.height = (border_width + view->xdg_surface->surface->current.height) *
					  output->output->scale};
		wlr_render_rect(view->server->renderer, &border_right, border_color,
						output->output->transform_matrix);
	}

	/* This calls the render_surface function for each surface amount the
	   xdg_surface's toplevel and popups. */
	struct render_data rdata = {
		.output = output->output, .view = view, .renderer = output->server->renderer, .when = &now};
	wlr_xdg_surface_for_each_surface(view->xdg_surface, render_surface, &rdata);
}

/* This function renderes an application surface */
void render_surface(struct wlr_surface *surface, int sx, int sy, void *data) {
	struct render_data *rdata = data;
	struct kwm_view *view = rdata->view;
	struct wlr_output *output = rdata->output;

	/* We first obtain a wlr_texture, which is a GPU resource. */
	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (texture == NULL) {
		return;
	}

	/* The view has a position in layout coordinates. If you have two displays,
	   one next to the other, both 1080p, a view on the right-most display might
	   have layout coordinates of 2000,100. We need to translate that to
	   output-local coordinates, or (2000-1928) */
	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(view->server->output_layout, output, &ox, &ox);
	ox += view->x + sx, oy += view->y + sy;

	/* We also have to apply the scale factor for HiDPI outputs. NOTE: HiDPI support incomplete */
	struct wlr_box box = {.x = ox * output->scale,
						  .y = oy * output->scale,
						  .width = surface->current.width * output->scale,
						  .height = surface->current.height * output->scale};

	/* wlr_matrix_project_box is a helper which takes a box with a desired
	   x, y coordinates, width and height, and an output geometry, then prepares
	   an orthographic projection and multiplies the necessary transforms to
	   produce a model-view-projection matrix. */
	float matrix[9];
	enum wl_output_transform transform = wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &box, transform, 0, output->transform_matrix);

	/* Take the matrix, texture, and an alpha and perform the actual rendering on the GPU */
	wlr_render_texture_with_matrix(rdata->renderer, texture, matrix, 1);

	/* Let the client know that we've displayed the frame and it can start preparing another one */
	wlr_surface_send_frame_done(surface, rdata->when);
}

/* This function is called every time the output is ready to display a frame */
void handle_output_frame(struct wl_listener *listener, void *data) {
	struct kwm_output *output = wl_container_of(listener, output, frame);
	struct wlr_renderer *renderer = output->server->renderer;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* wlr_output_attach_render makes the OpenGL context current */
	if (!wlr_output_attach_render(output->output, NULL)) {
		return;
	}

	/* The "effective" resolution can change if you rotate your outputs. */
	int width, height;
	wlr_output_effective_resolution(output->output, &width, &height);

	/* Begin the renderer (calls glViewport and some other GL checks */
	wlr_renderer_begin(renderer, width, height);

	/* Render the background color */
	float color[4] = {0.3, 0.3, 0.3, 1.0};
	wlr_renderer_clear(renderer, color);

	/* Render all of the views */
	struct kwm_view *view;
	wl_list_for_each_reverse(view, &output->server->views, link) { render_view(view, output, now); }

	/* If a hardware cursor is not supported then render a software cursor instead */
	wlr_output_render_software_cursors(output->output, NULL);

	/* Conclude rendering and swap the buffers */
	wlr_renderer_end(renderer);
	wlr_output_commit(output->output);
}

/* This function is called whenever a new display output is attached */
void handle_new_output(struct wl_listener *listener, void *data) {
	struct kwm_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (!wl_list_empty(&wlr_output->modes)) {
		struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
		wlr_output_set_mode(wlr_output, mode);
		wlr_output_enable(wlr_output, true);
		if (!wlr_output_commit(wlr_output)) {
			return;
		}
	}

	/* Allocates and configures state for this output */
	struct kwm_output *output = calloc(1, sizeof(struct kwm_output));
	output->output = wlr_output;
	output->server = server;

	/* Sets up a listener for the frame notify event */
	output->frame.notify = handle_output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&server->outputs, &output->link);

	/* Adds this output to the layout. The add_auto function arranges outputs from
	   left-to-right in the order they appear */
	wlr_output_layout_add_auto(server->output_layout, wlr_output);

	/* Creating the global adds a wl_output global to the display, which Wayland clients
	   can see to find out information about the output */
	wlr_output_create_global(wlr_output);
}

/* This function is called whenever a display is detached */
void handle_output_destroy(struct wl_listener *listener, void *data) {}

/* This function is called whenever a new pointer device becomes available */
void add_new_pointer(struct kwm_server *server, struct wlr_input_device *device) {
	/* We don't do anything special with pointers. All of our pointer handling is
	   proxied through wlr_cursor. This is where you should do libinput configuration. */
	wlr_log(WLR_DEBUG, "New pointer added");
	wlr_cursor_attach_input_device(server->cursor, device);
}

/* This function is called whenever a new input device becomes available */
void handle_new_input(struct wl_listener *listener, void *data) {
	struct kwm_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		add_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		add_new_pointer(server, device);
		break;
	default:
		break;
	}

	/* We need to let the seat know what the input capabilities are. There is always a cursor
	   even when there are no pointer devices, so we always include that capability */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

void process_cursor_motion(struct kwm_server *server, uint32_t time) {
	/* If the mode is non-passthrough, delegate to these functions */
	if (server->cursor_mode == KWM_CURSOR_MOVE) {
		process_cursor_move(server, time);
		return;
	} else if (server->cursor_mode == KWM_CURSOR_RESIZE) {
		process_cursor_resize(server, time);
		return;
	}
	/* Find the view under the pointer and send the event along */
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct kwm_view *view =
		desktop_view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!view) {
		/* If there is no view under the cursor, set the cursor image to default */
		wlr_xcursor_manager_set_cursor_image(server->cursor_mgr, "left_ptr", server->cursor);
	}
	if (surface) {
		bool focus_changed = seat->pointer_state.focused_surface != surface;
		/* "Enter" the surface if necessary. This lets the client know that the
		   cursor has entered one of its surfaces. */
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		if (!focus_changed) {
			/* The enter event contains coordinates, so we only need to notify
			   on motion if the focus did not change. */
			wlr_seat_pointer_notify_motion(seat, time, sx, sy);
		}
	} else {
		/* Clear pointer focus so future button events and such are not sent to the client */
		wlr_seat_pointer_clear_focus(seat);
	}
}

/* Moves the grabbed view to the new position */
void process_cursor_move(struct kwm_server *server, uint32_t time) {
	server->grabbed_view->x = server->cursor->x - server->grab_x;
	server->grabbed_view->y = server->cursor->y - server->grab_y;
}

/* Resizes the grabbed view */
void process_cursor_resize(struct kwm_server *server, uint32_t time) {}

/* This function is called when a pointer emits a _relative_
   pointer motion event (i.e. a delta) */
void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct kwm_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_event_pointer_motion *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	   handles constraining the motion to the output layout */
	wlr_cursor_move(server->cursor, event->device, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

/* This function is called when a pointer emits a _absolute_ motion event. For example
   this happens when you are running under another wayland window and you move the mouse
   over the window. You could enter the window from eany edge so we have to warp the
   mouse to the correct position */
void handle_cursor_motion_abs(struct wl_listener *listener, void *data) {
	struct kwm_server *server = wl_container_of(listener, server, cursor_motion_abs);
	struct wlr_event_pointer_motion_absolute *event = data;
	wlr_cursor_warp_absolute(server->cursor, event->device, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

/* This function is called whenever a mouse button is pressed */
void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct kwm_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_event_pointer_button *event = data;
	struct wlr_seat *seat = server->seat;
	double sx, sy;
	struct wlr_surface *surface;
	struct kwm_view *view =
		desktop_view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

	// seat->keyboard_state->keyboard
	/* Check if the mod key is being pressed */
	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(seat->keyboard_state.keyboard);
	if ((modifiers & WLR_MODIFIER_ALT) && event->state == WLR_KEY_PRESSED) {
		// server->cursor_mode = KWM_CURSOR_MOVE;
		begin_interactive(view, KWM_CURSOR_MOVE, 0);
		return;
		/* if alt is held down and thsi button was pressed, intercept it as a compositor binding */
		// for (int i = 0; i < nsyms; i++) {
		// 	handled = handle_keybinding(server, syms[i]);
		// }
	}

	/* if (!handled) { */
	/* 	/1* pass it along to the client *1/ */
	/* 	wlr_seat_set_keyboard(seat, keyboard->device); */
	/* 	wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state); */
	/* } */
	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);

	if (event->state == WLR_BUTTON_RELEASED) {
		/* If a button was released, we exit interactive move/resize mode */
		server->cursor_mode = KWM_CURSOR_PASSTHROUGH;
	} else {
		/* Focus the client if the button was pressed */
		focus_view(view, surface);
	}
}

/* This function is called whenever a mouse wheel is scrolled */
void handle_cursor_axis(struct wl_listener *listener, void *data) {}

/* This function is called when a pointer emits a frame event */
void handle_cursor_frame(struct wl_listener *listener, void *data) {
	struct kwm_server *server = wl_container_of(listener, server, cursor_frame);
	/* Notify the client with pointer focus of the frame event */
	wlr_seat_pointer_notify_frame(server->seat);
}

/* This function is called when the client provides a cursor image */
void handle_request_set_cursor(struct wl_listener *listener, void *data) {
	struct kwm_server *server = wl_container_of(listener, server, request_set_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;

	/* This can be sent by any client, so we check to make sure this one actually
	   has the pointer focus first */
	if (focused_client == event->seat_client) {
		/* Tell the cursor to use the provided surface as the cursor image */
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

/* This function is called when a modifier key, such as shift or alt is pressed */
void handle_keyboard_modifiers(struct wl_listener *listener, void *data) {
	struct kwm_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);

	/* A seat can only have one keyboard. We assign all connected keyboards to the same seat. */
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->device);

	/* Send modiifers to the client */
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
									   &keyboard->device->keyboard->modifiers);
}

/* This function is called when a key is pressed or released */
void handle_keyboard_key(struct wl_listener *listener, void *data) {
	struct kwm_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct kwm_server *server = keyboard->server;
	struct wlr_event_keyboard_key *event = data;
	struct wlr_seat *seat = server->seat;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);
	if ((modifiers & WLR_MODIFIER_ALT) && event->state == WLR_KEY_PRESSED) {
		/* if alt is held down and thsi button was pressed, intercept it as a compositor binding */
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, modifiers, syms[i]);
		}
	}

	if (!handled) {
		/* pass it along to the client */
		wlr_seat_set_keyboard(seat, keyboard->device);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

/* This function is called to register a new keyboard */
void add_new_keyboard(struct kwm_server *server, struct wlr_input_device *device) {
	struct kwm_keyboard *keyboard = calloc(1, sizeof(struct kwm_keyboard));
	keyboard->server = server;
	keyboard->device = device;

	/* Prepare an XKB keymap and assign it to the keyboard */
	struct xkb_rule_names rules = {0};
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap =
		xkb_map_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(device->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(device->keyboard, 25, 600);

	/* Set up listeners for keyboard events */
	keyboard->modifiers.notify = handle_keyboard_modifiers;
	wl_signal_add(&device->keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = handle_keyboard_key;
	wl_signal_add(&device->keyboard->events.key, &keyboard->key);

	wlr_seat_set_keyboard(server->seat, device);

	/* add the keyboard to our list of keyboards */
	wl_list_insert(&server->keyboards, &keyboard->link);
}

/* This function sets up an interactive move or resize operation, where the compositor
   stops propgating pointer events to clients and instead consumes them itself */
void begin_interactive(struct kwm_view *view, enum kwm_cursor_mode mode, uint32_t edges) {
	struct kwm_server *server = view->server;
	struct wlr_surface *focused_surface = server->seat->pointer_state.focused_surface;
	if (view->xdg_surface->surface != focused_surface) {
		/* Deny move/resize requests from unfocused clients */
		return;
	}
	server->grabbed_view = view;
	server->cursor_mode = mode;
	struct wlr_box geo_box;
	wlr_xdg_surface_get_geometry(view->xdg_surface, &geo_box);
	if (mode == KWM_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - view->x;
		server->grab_y = server->cursor->y - view->y;
	} else {
		server->grab_x = server->cursor->x + geo_box.x;
		server->grab_y = server->cursor->y + geo_box.y;
	}
	server->grab_width = geo_box.width;
	server->grab_height = geo_box.height;
	server->resize_edges = edges;
}

/* This function is called when a client would like to begin an interactive move. */
void handle_xdg_toplevel_request_move(struct wl_listener *listener, void *data) {
	struct kwm_view *view = wl_container_of(listener, view, request_move);
	begin_interactive(view, KWM_CURSOR_MOVE, 0);
}

/* This function is called when a client would like to resize their window */
void handle_xdg_toplevel_request_resize(struct wl_listener *listener, void *data) {
	struct kwm_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xdg_toplevel_resize_event *event = data;
	begin_interactive(view, KWM_CURSOR_RESIZE, event->edges);
}

/* This function is called when a surface is mapped, or ready to display on-screen */
void handle_xdg_surface_map(struct wl_listener *listener, void *data) {
	struct kwm_view *view = wl_container_of(listener, view, map);
	view->mapped = true;
	focus_view(view, view->xdg_surface->surface);
}

/* This function is called when a surface is unmapped, and should no longer be shown */
void handle_xdg_surface_unmap(struct wl_listener *listener, void *data) {
	struct kwm_view *view = wl_container_of(listener, view, unmap);
	view->mapped = false;
}

/* This function is called when the surface is destroyed and should never be shown again. */
void handle_xdg_surface_destroy(struct wl_listener *listener, void *data) {
	struct kwm_view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->link);
	free(view);
}

/* This function handles the XDG view decoration */
void handle_xdg_decoration(struct wl_listener *listener, void *data) {
	struct kwm_view *view = wl_container_of(listener, view, map);
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;

	/* Dont render the decoration */
	view->xdg_decoration = wlr_deco;
	wlr_xdg_toplevel_decoration_v1_set_mode(view->xdg_decoration,
											WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

/* This function is called whenever a new client (application window or poppup) is spawned */
void handle_new_xdg_surface(struct wl_listener *listener, void *data) {
	struct kwm_server *server = wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;
	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	/* Allocate a view for this surface */
	struct kwm_view *view = calloc(1, sizeof(struct kwm_view));
	view->server = server;
	view->xdg_surface = xdg_surface;

	/* Listen to the various events it can emit */
	view->map.notify = handle_xdg_surface_map;
	wl_signal_add(&xdg_surface->events.map, &view->map);

	view->unmap.notify = handle_xdg_surface_unmap;
	wl_signal_add(&xdg_surface->events.unmap, &view->unmap);

	view->destroy.notify = handle_xdg_surface_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &view->destroy);

	struct wlr_xdg_toplevel *toplevel = xdg_surface->toplevel;

	view->request_move.notify = handle_xdg_toplevel_request_move;
	wl_signal_add(&toplevel->events.request_move, &view->request_move);

	view->request_resize.notify = handle_xdg_toplevel_request_resize;
	wl_signal_add(&toplevel->events.request_resize, &view->request_resize);

	/* Add it to the list of views */
	wl_list_insert(&server->views, &view->link);
}

bool server_init(struct kwm_server *server) {
	wlr_log(WLR_DEBUG, "Initializing the wayland server...");

	/* The wayland display handles accepting clients from the Unix socket as well as
	   managing wayland globals etc */
	server->display = wl_display_create();

	/* The backend abstracts input and output hardware. The autocreate will choose the most
	   suitable backend based on the current environment. */
	server->backend = wlr_backend_autocreate(server->display, NULL);

	server->renderer = wlr_backend_get_renderer(server->backend);
	wlr_renderer_init_wl_display(server->renderer, server->display);

	/* This creates some hands-off wlroots interfaces. The compositor is necessary for
	   clients to allocate surfaces and the data device manager handles the clipboard */
	wlr_compositor_create(server->display, server->renderer);
	wlr_data_device_manager_create(server->display);

	/* Output Layout is a wlroots utility for working with an arrangment of screens
	   in a physical layout */
	server->output_layout = wlr_output_layout_create();

	/* Configure a listener to be notified when new outputs are available on the backend */
	wl_list_init(&server->outputs);
	server->new_output.notify = handle_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);

	/* Set up the list of views and the xdg-shell. The xdg-shell is a wayland protocol
	   which is used for application windows. */
	wl_list_init(&server->views);
	server->xdg_shell = wlr_xdg_shell_create(server->display);
	server->new_xdg_surface.notify = handle_new_xdg_surface;
	wl_signal_add(&server->xdg_shell->events.new_surface, &server->new_xdg_surface);

	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	/* Creates an xcursor manager which loads up xcursor themes to source cursor images */
	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	wlr_xcursor_manager_load(server->cursor_mgr, 1);

	/* wlr_cursor only displays an image on the screen. It does not move around automatically.
	   So we will need to attach input devices and handle movement */
	server->cursor_motion.notify = handle_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);

	server->cursor_motion_abs.notify = handle_cursor_motion_abs;
	wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_abs);

	server->cursor_button.notify = handle_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);

	server->cursor_axis.notify = handle_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);

	server->cursor_frame.notify = handle_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	/* Sets up the seat. The "seat" conceptually includes up to one keyboard, mouse etc */
	wl_list_init(&server->keyboards);

	server->new_input.notify = handle_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);

	server->seat = wlr_seat_create(server->display, "seat0");

	server->request_set_cursor.notify = handle_request_set_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_set_cursor);

	/* Set up the decoration manager */
	server->decoration_mgr = wlr_server_decoration_manager_create(server->display);
	wlr_server_decoration_manager_set_default_mode(server->decoration_mgr,
												   WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

	server->xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(server->display);
	server->new_xdg_decoration.notify = handle_xdg_decoration;
	wl_signal_add(&server->xdg_decoration_mgr->events.new_toplevel_decoration,
				  &server->new_xdg_decoration);

	return true;
}

int server_start(struct kwm_server *server, char *startup_cmd) {
	/* Add a Unix socket to the Wayland display */
	const char *socket = wl_display_add_socket_auto(server->display);
	if (!socket) {
		wlr_backend_destroy(server->backend);
		return 1;
	}

	if (!wlr_backend_start(server->backend)) {
		wlr_backend_destroy(server->backend);
		wl_display_destroy(server->display);
		return 1;
	}

	/* Set the WAYLAND_DISPLAY environment variable to our socket */
	setenv("WAYLAND_DISPLAY", socket, true);
	if (startup_cmd) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
		}
	}

	/* Run the wayland event loop */
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server->display);

	wl_display_destroy_clients(server->display);
	wl_display_destroy(server->display);
	wlr_backend_destroy(server->backend);

	return 0;
}
