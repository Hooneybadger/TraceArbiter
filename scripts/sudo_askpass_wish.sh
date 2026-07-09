#!/usr/bin/env bash
# X11 sudo askpass via Tcl/Tk. Prints the password to stdout for `sudo -A`.
# Does not store it.
export DISPLAY="${DISPLAY:-localhost:10.0}"
exec wish - <<'EOF'
package require Tk
wm title . "TraceArbiter sudo"
wm resizable . 0 0
label .l -justify left -text \
  "tracefs gid remount에 sudo가 필요합니다.\n비밀번호는 저장하지 않습니다."
entry .e -show "*" -width 32 -textvariable ::pw
frame .b
button .b.ok -text OK -width 8 -command {
    puts stdout $::pw
    flush stdout
    exit 0
}
button .b.cancel -text Cancel -width 8 -command { exit 1 }
pack .l -padx 12 -pady 8
pack .e -padx 12 -pady 4
pack .b.ok .b.cancel -side left -padx 6
pack .b -pady 8
bind .e <Return> { .b.ok invoke }
bind . <Escape> { exit 1 }
wm attributes . -topmost 1
focus -force .e
tkwait visibility .
EOF
