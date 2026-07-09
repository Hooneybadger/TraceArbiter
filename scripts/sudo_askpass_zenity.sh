#!/usr/bin/env bash
# Graphical sudo askpass for X11-forwarded lab sessions.
# Prints the password to stdout for `sudo -A`. Does not store it.
exec zenity --password --title="TraceArbiter: sudo for tracefs gid remount"
