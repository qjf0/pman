/* config: the action -> function deliver map */
#include "define.h"

static struct action deliver_map[] = {
	{ "to",      action_to },
	{ "replace", action_replace },
	{ "delete",  action_delete },
	{ NULL,      NULL },
};
