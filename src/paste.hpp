#ifndef _PASTE_HPP_
#define _PASTE_HPP_

#include <X11/Xlib.h>

void init_paste_dnd();
bool handle_drop_related_events(XEvent* ep);


#endif
