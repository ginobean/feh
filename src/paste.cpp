#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <map>
#include <iostream>
#include <cstdio>
#include <climits>
#include <cstring>

#include "paste.hpp"

using namespace std;

/*

Copying and pasting is in general a difficult problem: the application doing the
pasting has to first know where to get the data from, and then the two
applications (probably written by different people, maybe running on different
computers without a shared filesystem) communicate data in a format they bot
understand even though they are different applications.

The first three problems are solved by the X server: it mediates the
communication in a standard way. The last problem is solved by providing a
mechanism that allows the two programs to negotiate which data format to
transfer data in. Esentially, the pasting application asks for a list of
available formats, and then picks the one it deems most suitable. Unfortunately,
if both applications can grok types which are nearly equivalent, (such as
multiple image types), there is no way of telling which is best.

Anyway, in order to understand the details of how to operate this mechanism, a
little background is required.



A bit of background.
--------------------

Atoms
-----

The server contains a list of Atoms. An atom is a short string with an
assosciated number. The purpose of this is to avoid passing around
and comparing strings. It is done much more efficiently with atoms
instead, since only the 4 byte integer ID needs to be passsed and compared.

XInternAtom gets the atom number corresponding to a string.
XGetAtomName gets the string corresponding ot the atom number.

There is a single global list of atoms on the X server.


Properties
----------

_EACH_ window has a list of properties. Each list element contains an arbitrary
bunch of data with a numeric ID, a data type and a format type.  Unsuprisingly,
atoms are used to give names to these numeric IDs. In other words, the property
list is indexed by atoms---it's a list of name/value pairs. The data type is a
string containing a brief textual description of the data (eg a MIME type may be
used). Unsuprisingly again, this string is stored in an atom.  The format type
is the number of bits per element of the data and is either 8, 16 or 32.

The property data is read by XGetWindowProperty.

Certain (all?) properties can be written by any other program. So properties are
used to pass chunks of data between programs. This is how the clipboard works.


Selections
----------

If a bit of data is copied in one application, then the application grabs a
selection. There can be any number of selections, but there are several
predefined ones. Each selection has a name (ie an atom) identifing it. The two
useful selections are PRIMARY and CLIPBOARD. Highlight/middle click goes via
PRIMARY and explicit copy/paste goes via CLIPBOARD.

If you want to paste, then you need to get the selected stuff from the program
which owns the selection, and get it to convert it in to a format which you can
use. For this, you use XConvertSelection. But how does it know which format to
convert it in to? You tell it the name (ie atom) of the format you want. But how
do you know what to ask for? Well, first, you ask for a meta-format called
TARGETS. This causes the program to send you a list of the format names (atoms)
which it is able to convert to. You can then pick a suitable one from the list
and ask for it. When you ask for data using XConvertSelection, the program you
with the data received a SelectionNotify event.

All converted data is communicated via a property on the destination window.
This means you must have a window, but it does not have to be mapped.  You get
to choose which property you wish it to be communicated via. Once the property
has been filled up with the data, the program XSendEvent's you a SelectionNotify
event to tell you that the data is ready to be read. Now you have successfully
pasted some data.


Drag 'n Drop with XDND
----------------------

This is very similar to pasting, since the same negotiation of data types must
occur. Instead of asking for TARGETS, you instead read XdndTypeList on the
source window[1]. Then you call XConvertSelection using the XdndSelection
clipboard.

Stepping back a bit, XDND postdates X11 by many years, so all communication is
done via the generic ClientMessage events (instead of SelectionNotify events).
Windows announce their ability to accept drops by setting the XdndAware property
on the window (the value is a single atom, containing the version number).

When something is dragged over you, you are first sent an XdndEnter event. This
tells you the version number. This may also contain the first 3 types in the
XdndTypeList property, but it may not. This is the point at which XdndTypeList
should be read.

You will then be sent a stream of XdndPosition events. These will contain the
action requested and the position. You must reply with an XdndStatus message
stating whether a drop _could_ occur, and what action it will occur with.

Eventually, you will get an XdndLeave event, or an XdndDrop event. In the latter
case, you then call XConvertSelection. When the data arrives, you send an
XdndFinished event back.

Looking at it from the other side, if you initiate a drag, then the first thing
you do is grab the mouse. Since the mouse is grabbed, other programs will not
receive events, so you must send XDnD events to the correct window. When your
mouse pointer enters a window with the XDndAware property, you will send that
window an XDnDEnter event, informing it that a drag is in progress, and which
datatypes you can provide. As the mouse moves, you send XdndPosition events, and
the application replies with XdndStatus. If a drop is possible, then you will
change the mouse pointer to indicate this. If you leave the XdndAware window,
you send an XdndLeave event, and if the mouse button is released, you ungrab the
mouse and send an XdndDrop event (if a drop is possible). If a drop really
occurs, then the data transfer will proceed as a normal paste operation, using
the XdndSelection clipboard.


[1] XDnD also provides the first three targets in the first message it sends.
If it offers three or fewer targets, it may not provide XdndTypeList.

And here's how specifically:
*/



//Convert an atom name in to a std::string
static string GetAtomName(Display* disp, Atom a)
{
	if(a == None)
		return "None";
	else
		return XGetAtomName(disp, a);
}

struct Property
{
	unsigned char *data;
	int format, nitems;
	Atom type;
};


static Display* disp = NULL;
static Window root = None;

static Window window = None;
static Window drop_window = None;
static Window xdnd_source_window = None;

static int xdnd_version=0;
static Atom to_be_requested = None;
static bool emit_verbose_dnd_position_info = false;

//Atoms for Xdnd
static Atom XdndEnter = None;
static Atom XdndPosition = None;
static Atom XdndStatus = None;
static Atom XdndTypeList = None;
static Atom XdndActionCopy = None;
static Atom XdndDrop = None;
static Atom XdndLeave = None;
static Atom XdndFinished = None;
static Atom XdndSelection = None;
static Atom XdndProxy = None;
static Atom XdndAware = None;
static Atom PRIMARY = None;


static map<string, int> datatypes;

static bool sent_request = false;


//This atom isn't provided by default
static Atom XA_TARGETS;



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




//This fetches all the data from a property
Property read_property(Display* disp, Window w, Atom property)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *ret=0;

	int read_bytes = 1024;

	//Keep trying to read the property until there are no
	//bytes unread.
	do
	{
		if(ret != 0)
			XFree(ret);
		XGetWindowProperty(disp, w, property, 0, read_bytes, False, AnyPropertyType,
							&actual_type, &actual_format, &nitems, &bytes_after,
							&ret);

		read_bytes *= 2;
	}while(bytes_after != 0);

	cerr << endl;
	cerr << "Actual type: " << GetAtomName(disp, actual_type) << endl;
	cerr << "Actual format: " << actual_format << endl;
	cerr << "Number of items: " << nitems <<  endl;

	Property p = {ret, actual_format, (int) nitems, actual_type};

	return p;
}


// This function takes a list of targets which can be converted to (atom_list, nitems)
// and a list of acceptable targets with prioritees (datatypes). It returns the highest
// entry in datatypes which is also in atom_list: ie it finds the best match.
Atom pick_target_from_list(Display* disp, Atom* atom_list, int nitems, map<string, int> datatypes)
{
    cerr << "pick_target_from_list data_types size = " << datatypes.size() << endl;
    cerr << endl;
    cerr << "Contents of datatypes map:" << endl;
    for (auto const& x : datatypes) {
        cerr << " key = " << x.first << " , value = " << x.second << endl;
    }
    cerr << "---------------------------------------" << endl;

	//This is higger than the maximum priority.
	int priority=INT_MAX;

	for(int i=0; i < nitems; i++)
	{
		string atom_name = GetAtomName(disp, atom_list[i]);
		cerr << "Type " << i << " = " << atom_name << endl;

		//See if this data type is allowed and of higher priority (closer to zero)
		//than the present one.
		if(datatypes.find(atom_name)!= datatypes.end()) {
			if(datatypes[atom_name] < priority) {
				cerr << "Will request type: " << atom_name << endl;
				priority = datatypes[atom_name];
				to_be_requested = atom_list[i];
			}
        }
	}

	return to_be_requested;
}

// Finds the best target given up to three atoms provided (any can be None).
// Useful for part of the Xdnd protocol.
Atom pick_target_from_atoms(Display* disp, Atom t1, Atom t2, Atom t3, map<string, int> datatypes)
{
	Atom atoms[3];
	int  n=0;

	if(t1 != None)
		atoms[n++] = t1;

	if(t2 != None)
		atoms[n++] = t2;

	if(t3 != None)
		atoms[n++] = t3;

	return pick_target_from_list(disp, atoms, n, datatypes);
}


// Finds the best target given a local copy of a property.
Atom pick_target_from_targets(Display* disp, Property p, map<string, int> datatypes)
{
	//The list of targets is a list of atoms, so it should have type XA_ATOM
	//but it may have the type TARGETS instead.

	if((p.type != XA_ATOM && p.type != XA_TARGETS) || p.format != 32)
	{
		//This would be really broken. Targets have to be an atom list
		//and applications should support this. Nevertheless, some
		//seem broken (MATLAB 7, for instance), so ask for STRING
		//next instead as the lowest common denominator

        cerr << "pick_target_from_targets datatypes size = " << datatypes.size() << endl;

		if(datatypes.count("STRING"))
			return XA_STRING;
		else
			return None;
	}
	else
	{
		Atom *atom_list = (Atom*)p.data;

		return pick_target_from_list(disp, atom_list, p.nitems, datatypes);
	}
}


void
init_paste_dnd()
{
    //Atoms for Xdnd
    XdndEnter = XInternAtom(disp, "XdndEnter", False);
    XdndPosition = XInternAtom(disp, "XdndPosition", False);
    XdndStatus = XInternAtom(disp, "XdndStatus", False);
    XdndTypeList = XInternAtom(disp, "XdndTypeList", False);
    XdndActionCopy = XInternAtom(disp, "XdndActionCopy", False);
    XdndDrop = XInternAtom(disp, "XdndDrop", False);
    XdndLeave = XInternAtom(disp, "XdndLeave", False);
    XdndFinished = XInternAtom(disp, "XdndFinished", False);
    XdndSelection = XInternAtom(disp, "XdndSelection", False);
    XdndProxy = XInternAtom(disp, "XdndProxy", False);
    XdndAware = XInternAtom(disp, "XdndAware", False);
    PRIMARY = XInternAtom(disp, "PRIMARY", 0);

	//This is a meta-format for data to be "pasted" in to.
	//Requesting this format acquires a list of possible
	//formats from the application which copied the data.
	XA_TARGETS = XInternAtom(disp, "TARGETS", False);

    // This is the kind of data we're prepared to select
    // Each argument corresponds to a type, in order of preference
    // The key is the type the data is the priority.
    // lower numbers have a HIGHER priority than higher numbers,
    // 1 has HIGHER PRECEDENCE/PRIORITY than 2, etc.
    // here, we prefer to get 'text/uri-list' mime type over
    // getting the more generic 'string' type.
    datatypes["text/uri-list"] = 1;
    datatypes["STRING"] = 2;
}


bool
handle_drop_related_events(XEvent* ep)
{
    XEvent& event = *ep;

    if(event.type == ClientMessage)
    {
        if (event.xclient.message_type == XdndDrop) {
            cerr << "A ClientMessage has arrived:\n";
            cerr << "Type = " << GetAtomName(disp, event.xclient.message_type) << " (" << event.xclient.format << ")\n";
        }

        if(event.xclient.message_type == XdndEnter)
        {
            bool more_than_3 = event.xclient.data.l[1] & 1;
            Window source = event.xclient.data.l[0];

            cerr << hex << "Source window = 0x" << source << dec << endl;
            cerr << "Supports > 3 types = " << (more_than_3) << endl;
            cerr << "Protocol version = " << ( event.xclient.data.l[1] >> 24) << endl;
            cerr << "Type 1 = " << GetAtomName(disp, event.xclient.data.l[2]) << endl;
            cerr << "Type 2 = " << GetAtomName(disp, event.xclient.data.l[3]) << endl;
            cerr << "Type 3 = " << GetAtomName(disp, event.xclient.data.l[4]) << endl;

            xdnd_version = ( event.xclient.data.l[1] >> 24);

            //Query which conversions are available and pick the best

            if(more_than_3)
            {
                //Fetch the list of possible conversions
                //Notice the similarity to TARGETS with paste.
                Property p = read_property(disp, source , XdndTypeList);
                to_be_requested = pick_target_from_targets(disp, p, datatypes);
                XFree(p.data);
            }
            else
            {
                //Use the available list
                to_be_requested = pick_target_from_atoms(disp, event.xclient.data.l[2], event.xclient.data.l[3], event.xclient.data.l[4], datatypes);
            }


            cerr << "Requested type = " << GetAtomName(disp, to_be_requested) << endl;
        }
        else if(event.xclient.message_type == XdndPosition)
        {
            if (emit_verbose_dnd_position_info)
                emit_client_info(event, "XdndPosition");

            //Xdnd: reply with an XDND status message
            XClientMessageEvent m;
            memset(&m, 0, sizeof(m));
            m.type = ClientMessage;
            m.display = event.xclient.display;
            m.window = event.xclient.data.l[0];
            m.message_type = XdndStatus;
            m.format=32;
            m.data.l[0] = drop_window;
            m.data.l[1] = (to_be_requested != None);
            m.data.l[2] = 0; //Specify an empty rectangle
            m.data.l[3] = 0;
            m.data.l[4] = XdndActionCopy; //We only accept copying anyway.

            XSendEvent(disp, event.xclient.data.l[0], False, NoEventMask, (XEvent*)&m);
            XFlush(disp);
        }
        else if(event.xclient.message_type == XdndLeave)
        {
            //to_be_requested = None;

            //We can't actually reset to_be_requested, since OOffice always
            //sends this event, even when it doesn't mean to.
            cerr << "Xdnd cancelled.\n";
        }
        else if(event.xclient.message_type == XdndDrop)
        {
            if(to_be_requested == None)
            {
                //It's sending anyway, despite instructions to the contrary.
                //So reply that we're not interested.
                XClientMessageEvent m;
                memset(&m, 0, sizeof(m));
                m.type = ClientMessage;
                m.display = event.xclient.display;
                m.window = event.xclient.data.l[0];
                m.message_type = XdndFinished;
                m.format=32;
                m.data.l[0] = drop_window;
                m.data.l[1] = 0;
                m.data.l[2] = None; //Failed.
                XSendEvent(disp, event.xclient.data.l[0], False, NoEventMask, (XEvent*)&m);
            }
            else
            {
                xdnd_source_window = event.xclient.data.l[0];
                if(xdnd_version >= 1)
                    XConvertSelection(disp, XdndSelection, to_be_requested, PRIMARY, window, event.xclient.data.l[2]);
                else
                    XConvertSelection(disp, XdndSelection, to_be_requested, PRIMARY, window, CurrentTime);
            }
        }
    }

    if(event.type == SelectionNotify)
    {
        Atom target = event.xselection.target;

        cerr << "A selection notify has arrived!\n";
        cerr << hex << "Requestor = 0x" << event.xselectionrequest.requestor << dec << endl;
        cerr << "Selection atom = " << GetAtomName(disp, event.xselection.selection) << endl;
        cerr << "Target atom    = " << GetAtomName(disp, target)    << endl;
        cerr << "Property atom  = " << GetAtomName(disp, event.xselection.property) << endl;

        if(event.xselection.property == None)
        {
            //If the selection can not be converted, quit with error 2.
            //If TARGETS can not be converted (nothing owns the selection)
            //then quit with code 3.
            return true;
        }
        else
        {
            Property prop = read_property(disp, window, PRIMARY);

            //If we're being given a list of targets (possible conversions)
            if(target == XA_TARGETS && !sent_request)
            {
                sent_request = 1;
                to_be_requested = pick_target_from_targets(disp, prop, datatypes);

                if(to_be_requested == None)
                {
                    cerr << "No matching datatypes.\n";
                    return true;

                }
                else //Request the data type we are able to select
                {
                    cerr << "Now requsting type " << GetAtomName(disp, to_be_requested) << endl;
                    XConvertSelection(disp, PRIMARY, to_be_requested, PRIMARY, window, CurrentTime);
                }
            }
            else if(target == to_be_requested)
            {
                //Dump the binary data
                cerr << "Data begins:" << endl;
                cerr << "--------\n";
                cout << "[";
                cout.write((char*)prop.data, prop.nitems * prop.format/8);
                cout << "]";
                cout << flush;
                cerr << endl << "--------" << endl << "Data ends\n";
                XFree(prop.data);

                //Reply OK.
                XClientMessageEvent m;
                memset(&m, 0, sizeof(m));
                m.type = ClientMessage;
                m.display = disp;
                m.window = xdnd_source_window;
                m.message_type = XdndFinished;
                m.format=32;
                m.data.l[0] = window;
                m.data.l[1] = 1;
                m.data.l[2] = XdndActionCopy; //We only ever copy.

                //Reply that all is well.
                XSendEvent(disp, xdnd_source_window, False, NoEventMask, (XEvent*)&m);
                XSync(disp, False);
            }
        }
    }

    return true;
}



int paste_main(int argc, char ** argv)
{
    int screen;
    XEvent e;

	//The usual Xinit stuff...
	disp = XOpenDisplay(NULL);
	screen = DefaultScreen(disp);
	root = RootWindow(disp, screen);

	//We need a target window for the pasted data to be sent to.
	//However, this does not need to be mapped.

    auto border_color = BlackPixel(disp, screen);
    // shade of gray
    auto fill_color = 0x303030;
    window = XCreateSimpleWindow(disp, root, 0, 0, 400, 400, 0, border_color, fill_color);

    init_paste_dnd();

    //If we're doing DnD, instead of normal paste, then we need a window to drop in.
    XMapWindow(disp, window);
    drop_window = window;
    //Announce XDND support
    Atom version=5;
    XChangeProperty(disp, window, XdndAware, XA_ATOM, 32, PropModeReplace, (unsigned char*)&version, 1);

	XFlush(disp);

	for(;;)
	{
		XNextEvent(disp, &e);
                handle_drop_related_events(&e);
	}
}
