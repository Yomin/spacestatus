# spacestatus

Simple space status trayicon.

## compile

Use autogen.sh to generate Makefile. It'll ask you for optional features
like notification or bubbles.

## usage

Invoke with the URL to your space API eg.

spacestatus spaceapi.space.net

## ico

The icons are accessed at $(bin)/../share/{open,closed,pending}.xpm.
Replace/modify to change look.

## options

 - -r refresh in minutes
 - -p port
 - -t notification/bubble timeout
 - -y tooltip y position
 - -c tooltip foreground color
 - -b tooltip background color
 - -f tooltip font
