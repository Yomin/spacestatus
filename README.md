# spacestatus

Simple space status trayicon.

## usage

Place three xpm files named open.xpm, closed.xpm and pending.xpm next to the executable.
Invoke with the URL to your space API eg.

spacestatus spaceapi.space.net

## notifications/bubbles

Compile with 'make notify' or 'make bubble' to activate respective function.

## options

 - -r refresh in minutes
 - -p port
 - -t notification/bubble timeout
 - -y tooltip y position
 - -c tooltip foreground color
 - -b tooltip background color
 - -f tooltip font
