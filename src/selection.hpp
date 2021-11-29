#ifndef _PASTE_HPP_
#define _PASTE_HPP_

#include <X11/Xlib.h>
#include <string>

void set_current_file_uri(const std::string& uri);

void init_selection_x_vars(Display *d, Window w);
void init_selection_dnd();
bool handle_drag_related_events(XEvent* ep);


#endif
