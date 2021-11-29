#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "selection.hpp"

using namespace std;

//See paste.cc for a description of how the copy/paste and XDnD state machine works.

//See process_selection_request to see how to perform a paste when a SelectionNotify
//event arrives.

//see main for a sample implementation of an Xdnd state machine.

//The three states of Xdnd: we're over a window which does not
//know about XDnD, we're over a window which does know, but won't
//allow a drop (because we offer no suitable datatype), or we're
//over a window which will accept a drop.
#define UNAWARE 0
#define UNRECEPTIVE 1
#define CAN_DROP 2


bool emit_verbose_dnd_status_info = false;

static Display* disp = NULL;
static Window root = None;
static Window window = None;
static Window previous_window = None; // Window found by the last MotionNotify event.
static Window drag_to_window= None;

static bool dragging=0;                   //Are we currently dragging
static int previous_window_version = -1;         //XDnD version of previous_window
static int status=UNAWARE;

static Cursor grab_bad = None;
static Cursor grab_maybe = None;
static Cursor grab_good = None;


//Define atome not defined in Xatom.h
static Atom XA_TARGETS = None;
static Atom XA_multiple = None;
static Atom XA_image_bmp = None;
static Atom XA_image_jpg = None;
static Atom XA_image_tiff = None;
static Atom XA_image_png = None;
static Atom XA_text_uri_list = None;
static Atom XA_text_uri = None;
static Atom XA_text_plain = None;
static Atom XA_text = None;

static Atom XA_XdndSelection = None;
static Atom XA_XdndAware = None;
static Atom XA_XdndEnter = None;
static Atom XA_XdndLeave = None;
static Atom XA_XdndTypeList = None;
static Atom XA_XdndPosition = None;
static Atom XA_XdndActionCopy = None;
static Atom XA_XdndStatus = None;
static Atom XA_XdndDrop = None;
static Atom XA_XdndFinished = None;

static map<Atom, string> typed_data;




//Utility function for getting the atom name as a string.
static string GetAtomName(Display* disp, Atom a)
{
	if(a == None)
		return "None";
	else
		return XGetAtomName(disp, a);
}


static void
emit_client_info(XEvent& event, string eventDescription)
{
    cout  << eventDescription << " event received" << endl
          << "    Target window           = 0x" << hex << event.xclient.data.l[0] << dec << endl
          << "    Will accept             = " << (event.xclient.data.l[1] & 1)  << endl
          << "    No rectangle of silence = " << (event.xclient.data.l[1] & 2)  << endl
          << "    Rectangle of silence x  = " << (event.xclient.data.l[2] >> 16)    << endl
          << "    Rectangle of silence y  = " << (event.xclient.data.l[2] & 0xffff)    << endl
          << "    Rectangle of silence w  = " << (event.xclient.data.l[3] >> 16)    << endl
          << "    Rectangle of silence h  = " << (event.xclient.data.l[3] & 0xffff)    << endl
          << "    Action                  = " << GetAtomName(disp, event.xclient.data.l[4]) << endl;
}


bool set_current_file_uri(std::string& uri)
{
    if (uri.find("://") == std::string::npos) {
        uri.insert(0, "file://");
    }

    if (typed_data[XA_text_uri_list] != uri) {
        typed_data[XA_text_uri_list] = uri;
        return true;
    }

    return false;
}


//A simple, inefficient function for reading a
//whole file in to memory
string read_whole_file(const string& name, string& fullname)
{
	ostringstream f;
	ifstream file;

	//Try in the current directory first, then in the data directory
	{
		vector<char> buf(4096, 0);
		getcwd(&buf[0], 4095);
		fullname = &buf[0] + string("/") + name;
	}

	file.open(fullname.c_str(), ios::binary);

	// if(!file.good())
	// {
	// 	fullname = DATADIR + name;
	// 	file.open(fullname.c_str(), ios::binary);
	// }

	f << file.rdbuf();

	return f.str();
}


//Construct a list of targets and place them in the specified property This
//consists of all datatypes we know of as well as TARGETS and MULTIPLE. Reading
//this property tell the application wishing to paste which datatypes we offer.
void set_targets_property(Display* disp, Window w, map<Atom, string>& typed_data, Atom property)
{

	vector<Atom> targets;

	for(map<Atom,string>::const_iterator i=typed_data.begin(); i != typed_data.end(); i++)
		targets.push_back(i->first);


	cout << "Offering: ";
	for(unsigned int i=0; i < targets.size(); i++)
		cout << GetAtomName(disp, targets[i]) << "  ";
	cout << endl;

	//Fill up this property with a list of targets.
	XChangeProperty(disp, w, property, XA_ATOM, 32, PropModeReplace,
					(unsigned char*)&targets[0], targets.size());
}



//This function essentially performs the paste operation: by converting the
//stored data in to a format acceptable to the destination and replying
//with an acknowledgement.
void process_selection_request(XEvent e, map<Atom, string>& typed_data)
{

	if(e.type != SelectionRequest)
		return;

	//Extract the relavent data
	Window owner     = e.xselectionrequest.owner;
	Atom selection   = e.xselectionrequest.selection;
	Atom target      = e.xselectionrequest.target;
	Atom property    = e.xselectionrequest.property;
	Window requestor = e.xselectionrequest.requestor;
	Time timestamp   = e.xselectionrequest.time;
	Display* disp    = e.xselection.display;

	cout << "A selection request has arrived!\n";
	cout << hex << "Owner = 0x" << owner << endl;
	cout << "Selection atom = " << GetAtomName(disp, selection) << endl;
	cout << "Target atom    = " << GetAtomName(disp, target)    << endl;
	cout << "Property atom  = " << GetAtomName(disp, property) << endl;
	cout << hex << "Requestor = 0x" << requestor << dec << endl;
	cout << "Timestamp = " << timestamp << endl;


	//X should only send requests for the selections since we own.
	//since we own exaclty one, we don't need to check it.

	//Replies to the application requesting a pasting are XEvenst
	//sent via XSendEvent
	XEvent s;

	//Start by constructing a refusal request.
	s.xselection.type = SelectionNotify;
	//s.xselection.serial     - filled in by server
	//s.xselection.send_event - filled in by server
	//s.xselection.display    - filled in by server
	s.xselection.requestor = requestor;
	s.xselection.selection = selection;
	s.xselection.target    = target;
	s.xselection.property  = None;   //This means refusal
	s.xselection.time      = timestamp;



	if(target ==XA_TARGETS)
	{
		cout << "Replying with a target list.\n";
		set_targets_property(disp, requestor, typed_data, property);
		s.xselection.property = property;
	}
	else if(typed_data.count(target))
	{
		//We're asked to convert to one the formate we know about
		cout << "Replying with which ever data I have" << endl;

		//Fill up the property with the URI.
		s.xselection.property = property;
		XChangeProperty(disp, requestor, property, target, 8, PropModeReplace,
						reinterpret_cast<const unsigned char*>(typed_data[target].c_str()), typed_data[target].size());
	}
	else if(target == XA_multiple)
	{
		//In this case, the property has been filled up with a list
		//of atom pairs. The pairs being (target, property). The
		//processing should continue as if whole bunch of
		//SelectionRequest events had been received with the
		//targets and properties specified.

		//The ICCCM is rather ambiguous and confusing on this particular point,
		//and I've never encountered a program which requests this (I can't
		//test it), so I haven't implemented it.

		cout << "MULTIPLE is not implemented. It should be, according to the ICCCM, but\n"
			 << "I've never encountered it, so I can't test it.\n";
	}
	else
	{
		//We've been asked to converto to something we don't know
		//about.
		cout << "No valid conversion. Replying with refusal.\n";
	}

	//Reply
	XSendEvent(disp, e.xselectionrequest.requestor, True, 0, &s);
	cout << endl;
}


//Find the applications top level window under the mouse.
Window find_app_window(Display* disp, Window w)
{
	//Drill down the windows under the mouse, looking for
	//the window with the XdndAware property.

	int nprops, i=0;
	Atom* a;

	if(w == 0)
		return 0;

	//Search for the WM_STATE property
	a = XListProperties(disp, w, &nprops);
	for(i=0; i < nprops; i++)
		if(a[i] == XA_XdndAware)
			break;

	if(nprops)
		XFree(a);

	if(i != nprops)
		return w;

	//Drill down one more level.
	Window child, wtmp;
	int tmp;
	unsigned int utmp;
	XQueryPointer(disp, w, &wtmp, &child, &tmp, &tmp, &tmp, &tmp, &utmp);

	return find_app_window(disp, child);
}



void
init_selection_x_vars(Display *d, Window w)
{
    int screen;

    disp = d;
    screen = DefaultScreen(disp);
    root = RootWindow(disp, screen);
    window = w;
}


void
init_selection_dnd()
{
	//Create three cursors for the three different XDnD states.
	//I think a turkey is a good choice for a program which doesn't
	//understand Xdnd.
	grab_bad =XCreateFontCursor(disp, XC_gobbler);
	grab_maybe =XCreateFontCursor(disp, XC_circle);
	grab_good =XCreateFontCursor(disp, XC_sb_down_arrow);

	//None of these atoms are provided in Xatom.h
	XA_TARGETS = XInternAtom(disp, "TARGETS", False);
	XA_multiple = XInternAtom(disp, "MULTIPLE", False);
	XA_image_bmp = XInternAtom(disp, "image/bmp", False);
	XA_image_jpg = XInternAtom(disp, "image/jpeg", False);
	XA_image_tiff = XInternAtom(disp, "image/tiff", False);
	XA_image_png = XInternAtom(disp, "image/png", False);
	XA_text_uri_list = XInternAtom(disp, "text/uri-list", False);
	XA_text_uri= XInternAtom(disp, "text/uri", False);
	XA_text_plain = XInternAtom(disp, "text/plain", False);
	XA_text = XInternAtom(disp, "TEXT", False);
	XA_XdndSelection = XInternAtom(disp, "XdndSelection", False);
	XA_XdndAware = XInternAtom(disp, "XdndAware", False);
	XA_XdndEnter = XInternAtom(disp, "XdndEnter", False);
	XA_XdndLeave = XInternAtom(disp, "XdndLeave", False);
	XA_XdndTypeList = XInternAtom(disp, "XdndTypeList", False);
	XA_XdndPosition = XInternAtom(disp, "XdndPosition", False);
	XA_XdndActionCopy = XInternAtom(disp, "XdndActionCopy", False);
	XA_XdndStatus = XInternAtom(disp, "XdndStatus", False);
	XA_XdndDrop = XInternAtom(disp, "XdndDrop", False);
	XA_XdndFinished = XInternAtom(disp, "XdndFinished", False);

	//Create a mapping between the data type (specified as an atom) and the
	//actual data. The data consists of a prespecified list of files in the
	//current or install directory, and the URL of the PNG, in various
	//incarnations.

	XFlush(disp);
}

bool
handle_drag_related_events(XEvent* ep)
{
    XEvent& event = *ep;

    if(event.type == SelectionRequest)
    {
        //A request to select and drag, to a possibly different app,  has occured.
        process_selection_request(event, typed_data);
    }
    else if(event.type == MotionNotify && dragging == 0)
    {
        if (event.xmotion.state & Button1Mask) {
            if(XGrabPointer(disp, window, True, Button1MotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, root, grab_bad, CurrentTime) == GrabSuccess)
            {
                dragging=1;
                XSetSelectionOwner(disp, XA_XdndSelection, window, CurrentTime);
                cout << "Begin dragging.\n\n";
            }
            else
                cout << "Grab failed!\n\n";
        }
    }
    else if(event.type == MotionNotify)
    {
        Atom atmp;
        int window_version=-1;
        int fmt;
        unsigned long nitems, bytes_remaining;
        unsigned char *data = 0;

        //Look for XdndAware in the window under the pointer. So, first,
        //find the window under the pointer.
        drag_to_window = find_app_window(disp, root);
        if (drag_to_window != None) {
            cout << "Application window is: 0x" << hex << drag_to_window << dec << endl;
        }

        if(drag_to_window == previous_window)
            window_version = previous_window_version;
        else if(drag_to_window == None)
            ;
        else if(XGetWindowProperty(disp, drag_to_window, XA_XdndAware, 0, 2, False, AnyPropertyType, &atmp, &fmt, &nitems, &bytes_remaining, &data) != Success)
            cout << "Property read failed.\n";
        else if(data == 0)
            cout << "Property read failed.\n";
        else if(fmt != 32)
            cout << "XdndAware should be 32 bits, not " << fmt << " bits\n";
        else if(nitems != 1)
            cout << "XdndAware should contain exactly 1 item, not " << nitems << " items\n";
        else
        {
            window_version = data[0];
            cout << "XDnD window_version is " << window_version << endl;
        }

        if(status == UNAWARE && window_version != -1)
            status = UNRECEPTIVE;
        else if(window_version == -1)
            status = UNAWARE;

        //Update the pointer state.
        if(status == UNAWARE)
            XChangeActivePointerGrab(disp, Button1MotionMask | ButtonReleaseMask, grab_bad, CurrentTime);
        else if(status == UNRECEPTIVE)
            XChangeActivePointerGrab(disp, Button1MotionMask | ButtonReleaseMask, grab_maybe, CurrentTime);
        else
            XChangeActivePointerGrab(disp, Button1MotionMask | ButtonReleaseMask, grab_good, CurrentTime);



        if(drag_to_window != previous_window && previous_window_version != -1)
        {
            cout << "Left window 0x" << hex << previous_window  << dec << ": sending XdndLeave\n";
            //We've left an old, aware window.
            //Send an XDnD Leave

            XClientMessageEvent m;
            memset(&m, 0, sizeof(m));
            m.type = ClientMessage;
            m.display = event.xclient.display;
            m.window = previous_window;
            m.message_type = XA_XdndLeave;
            m.format=32;
            m.data.l[0] = window;
            m.data.l[1] = 0;
            m.data.l[2] = 0;
            m.data.l[3] = 0;
            m.data.l[4] = 0;

            XSendEvent(disp, previous_window, False, NoEventMask, (XEvent*)&m);
            XFlush(disp);
        }

        if(drag_to_window != previous_window && window_version != -1)
        {
            cout << "Entered window 0x" << hex << drag_to_window  << dec << ": sending XdndLeave\n";
            //We've entered a new, aware drag_to_window.
            //Send an XDnD Enter event.
            map<Atom, string>::const_iterator i = typed_data.begin();

            XClientMessageEvent m;
            memset(&m, 0, sizeof(m));
            m.type = ClientMessage;
            m.display = event.xclient.display;
            m.window = drag_to_window;
            m.message_type = XA_XdndEnter;
            m.format=32;
            m.data.l[0] = window;
            m.data.l[1] = min(5, window_version) << 24  |  (typed_data.size() > 3);
            m.data.l[2] = typed_data.size() > 0 ? i++->first : 0;
            m.data.l[3] = typed_data.size() > 1 ? i++->first : 0;
            m.data.l[4] = typed_data.size() > 2 ? i->first : 0;


            cout << "   window_version  = " << min(5, window_version) << endl
                 << "   >3 types = " << (typed_data.size() > 3) << endl
                 << "   Type 1   = " << GetAtomName(disp, m.data.l[2]) << endl
                 << "   Type 2   = " << GetAtomName(disp, m.data.l[3]) << endl
                 << "   Type 3   = " << GetAtomName(disp, m.data.l[4]) << endl;

            XSendEvent(disp, drag_to_window, False, NoEventMask, (XEvent*)&m);
            XFlush(disp);
        }

        if(window_version != -1)
        {
            //Send an XdndPosition event.
            //
            // We're being abusive, and ignoring the
            // rectangle of silence.


            int root_x, root_y;
            int x, y;
            unsigned int utmp;
            Window wtmp;

            XQueryPointer(disp, drag_to_window, &wtmp, &wtmp, &root_x, &root_y, &x, &y, &utmp);
            XClientMessageEvent m;
            memset(&m, 0, sizeof(m));
            m.type = ClientMessage;
            m.display = event.xclient.display;
            m.window = drag_to_window;
            m.message_type = XA_XdndPosition;
            m.format=32;
            m.data.l[0] = window;
            m.data.l[1] = 0;
            m.data.l[2] = (root_x <<16) | root_y;

            m.data.l[3] = CurrentTime; //Our data is not time dependent, so send a generic timestamp;
            m.data.l[4] = XA_XdndActionCopy;

            cerr << "Sending XdndPosition" << endl
                 << "    x      = " << x << endl
                 << "    y      = " << y << endl
                 << "    Time   = " << m.data.l[3] << endl
                 << "    Action = " << GetAtomName(disp, m.data.l[4]) << endl;

            XSendEvent(disp, drag_to_window, False, NoEventMask, (XEvent*)&m);
            XFlush(disp);

        }

        previous_window = drag_to_window;
        previous_window_version = window_version;
//			cout << endl;
    }
    else if(dragging && event.type == ButtonRelease && event.xbutton.button == 1)
    {
        cout << "Mouse button was released.\n";


        if(status == CAN_DROP)
        {
            cout << "Perform drop:\n";

            XClientMessageEvent m;
            memset(&m, 0, sizeof(m));
            m.type = ClientMessage;
            m.display = event.xclient.display;
            m.window = previous_window;
            m.message_type = XA_XdndDrop;
            m.format=32;
            m.data.l[0] = window;
            m.data.l[1] = 0;
            m.data.l[2] = CurrentTime; //Our data is not time dependent, so send a generic timestamp;
            m.data.l[3] = 0;
            m.data.l[4] = 0;

            XSendEvent(disp, previous_window, False, NoEventMask, (XEvent*)&m);
            XFlush(disp);
        }


        XUngrabPointer(disp, CurrentTime);
        dragging=0;
        status=UNAWARE;
        previous_window=None;
        previous_window_version=-1;
//			cout << endl;
    }
    else if(event.type == ClientMessage && event.xclient.message_type == XA_XdndStatus)
    {
        if (emit_verbose_dnd_status_info) {
            emit_client_info(event, "XdndStatus");
        }

        if( (event.xclient.data.l[1] & 1) == 0 &&  event.xclient.data.l[4] != None)
        {
            cout << "Action is given, even though the target won't accept a drop.\n";
        }


        if(dragging)
        {
            if((event.xclient.data.l[1]&1) && status != UNAWARE)
                status = CAN_DROP;

            if(!(event.xclient.data.l[1]&1) && status != UNAWARE)
                status = UNRECEPTIVE;
        }

        if(!dragging)
            cout << "Message received, but dragging is not active!\n";

        if(status == UNAWARE)
            cout << "Message received, but we're not in an aware window!\n";

//			cout << endl;
    }
    else if(event.type == ClientMessage && event.xclient.message_type == XA_XdndFinished)
    {
        //Check for these messages. Since out data is static, we don't need to do anything.
        cout  << "XDnDFinished event received:" << endl
              << "    Target window           = 0x" << hex << event.xclient.data.l[0] << dec << endl
              << "    Was successful          = " << (event.xclient.data.l[1] & 1)  << endl
              << "    Action                  = " << GetAtomName(disp, event.xclient.data.l[2]) << endl;
    }

    return true;
}


int selection_main(int argc, char**argv)
{
	int screen;
	XEvent event;

	//Standard X init stuff
	disp = XOpenDisplay(NULL);
	screen = DefaultScreen(disp);
	root = RootWindow(disp, screen);

	//A window is required to perform copy/paste operations
	//but it does not need to be mapped.
    auto border_color = BlackPixel(disp, screen); // shade of gray
    auto fill_color = 0x228b22; // bright green
    window = XCreateSimpleWindow(disp, root, 0, 0, 400, 400, 0, border_color, fill_color);
    //We need to map the window to drag from
    XMapWindow(disp, window);
    XSelectInput(disp, window, ButtonPressMask | Button1MotionMask | ButtonReleaseMask);

    init_selection_dnd();

	for(;;)
	{
		XNextEvent(disp, &event);
        handle_drag_related_events(&event);
	} // end for.

	return 0;
}
