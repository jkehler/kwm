#ifndef KWM_CONFIG_H
#define KWM_CONFIG_H

#include "kwm.h"

#define MODKEY		WLR_MODIFIER_ALT
#define SHIFTKEY	WLR_MODIFIER_SHIFT

static const char *termcmd[] = { "alacritty", NULL };
const keybind keybinds[] = {
	{ MODKEY,			XKB_KEY_Return,		kwm_spawn_process,		{ .v = termcmd } },
	{ MODKEY|SHIFTKEY,	XKB_KEY_E,			kwm_exit,				{0} }
};

#endif
