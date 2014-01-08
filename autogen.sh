
options=""

function option () {
    local x
    echo -n "$1? "
    [ $2 -eq 1 ] && echo -n "[Y/n] " || echo -n "[y/N] "
    read x
    [ "x$x" = "xY" -o "x$x" = "xy" ] && options="$options --with-$3"
    [ $2 -eq 1 -a "x$x" = "x" ] && options="$options --with-$3"
}

option "Notification" 1 "notify"
option "Bubble" 0 "bubble"
option "XEmbed" 0 "xembed"

autoreconf -vi || exit 1

./configure $options $*
