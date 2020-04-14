#ifndef KWM_H
#define KWM_H

#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_keyboard.h>

#define LENGTH(X)               (sizeof X / sizeof X[0])

typedef struct {
	const char *symbol;
} layout;

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} arg;


typedef struct {
	uint32_t 		modifiers;
	xkb_keysym_t	keysym;
	void 			(*func)(struct kwm_server *server, const arg *);
	const arg		arg;
} keybind;

bool handle_keybinding(struct kwm_server *server, uint32_t modifiers, xkb_keysym_t sym);
void kwm_spawn_process(struct kwm_server *server, const arg *arg);
void kwm_exit(struct kwm_server *server, const arg *arg);
void kwm_kill_view(struct kwm_server *server, const arg *arg);

#endif
