gExec
=====

Debian Buster repository contains a gexec package which requires pkexec.
gksu has been removed from Debian, as of Debian 10 (Buster).
Under the status quo, the Debian-packageded gexec "Run as root" functionality
is unusable in the absence of gksu.

For this (v1.5.4) gexec, "Run as root" is hardcoded to use gksu.

Changes in version 0.5.4:

* a detailed list of changes is provided within the debian/changelog file

Version           : 0.5.4
Original Author   : Ferry Boender ( see: http://www.electricmonk.nl )
License           : General Public License Version 2
homepage          : http://www.gitlab.com/skidoo/gexec


Introduction
------------

gExec is a "command runner" utility. Typically, it is assigned to a global
keybind (e.g Alt+F2). It presents a GUI containing an entrybox into which
you can type a command (aka commandstring, launchstring) along with toggle
buttons for optionally specifying "Run in terminal", "Run as root" (via gksu),
and/or "Keep Open this dialog after launch".


gExec uses GTK2 toolkit for its GUI; it is suitable for use with(in) a range of
window managers and desktop environments, including WindowMaker, iceWM, fluxbox,
JWM, et al.


Installation
------------

### Requirements

In order to compile gExec, you will need:

*   Gtk 2.0+ development headers
*   Glib 2.0+ development headers

In order to run gExec, you will need:

*   Gtk 2.0+ dynamically loadable libraries


### Compiling (instructions for non- Debian apt//dpkg systems)

To compile gExec, just type 'make' at the command line in the source directory.

### Installation (instructions for non- Debian apt//dpkg systems)

gExec is composed of only a single binary. You can copy it to any destination
on your system (suggested placement: /usr/local/bin)
Typing 'make install' at the command line in the source directory
will result in binary being copied to /usr/local/bin.


### Usage

From the --help output:

	Usage:
	  gexec [OPTION...] - Interactive run dialog

	Help Options:
	  -?, --help               Show help options
	  -k, --keepopen           Keep gExec open after executing a command

Configuration
-------------

gExec has a few configuration options which are stored in the file

    $HOME/.config/gexec/gexec.conf

This file will be created automically, if absent, the first time gExec is run.
It can contain these options:

	cmd_termemu=STRING   (example value:  xterm -e %s  )
	history_max=INT      (default value:  40  )

If %s is present, it will be replaced with the commandstring entered by the user.
	
Please be careful about the	construction of the commands for the terminal emulator
and the gksu program. Not all programs will work nicely together; some commandstrings
must be carefully crafted, with attention to appropriate inclusion of
quotationmark characters and exclusion of commandstring characters disallowed by
the receiving program. After gexec has launched and disowned a given command,
it does not (as a practical matter, cannot) provide further feedback//prompt if
subsequent failure to execute a command via terminal or as root occurs.


License
-------

gExec is Copyright by Ferry Boender, licensed under the General Public License (GPL)

	Copyright (C), 2004 by Ferry Boender

	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the Free
	Software Foundation; either version 2 of the License, or (at your option)
	any later version.

	This program is distributed in the hope that it will be useful, but
	WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
	or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
	for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	675 Mass Ave, Cambridge, MA 02139, USA.

	For more information, see the COPYING file supplied with this program.

