/*
 * Copyright (c) 2013, 2014 Martin Rödel aka Yomin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <libgen.h>
#include <poll.h>
#include <resolv.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/xpm.h>

#ifdef HAVE_CONFIG_H
#   include "config.h"
#endif

#ifdef NOTIFY
#   include <libnotify/notify.h>
#endif

#include "json.h"

#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

#define PENDING 0
#define OPEN    1
#define CLOSED  2
#define LOST    3

#define BORDER 3 // tooltip

#define OPTSTR_ALL "v::r:p:y:c:b:f:"
#define USAGE_ALL "Usage:\t%s [-v] [-r <min>] [-p <port>]" \
    "[-y <pixel>] [-c <rgb>] [-b <rgb>] [-f <font>]"
#define USAGE_PARAM "<dest>"
#if defined NOTIFY || defined BUBBLE
#   define OPTSTR OPTSTR_ALL "t:"
#   define USAGE USAGE_ALL "[-t <sec>] " USAGE_PARAM "\n"
#   define NOTIFYBUBBLE1(...) __VA_ARGS__
#else
#   define OPTSTR OPTSTR_ALL
#   define USAGE USAGE_ALL " " USAGE_PARAM "\n"
#   define NOTIFYBUBBLE1(...)
#endif

#ifdef NOTIFY
#   define NOTIFY1(...) __VA_ARGS__
#else
#   define NOTIFY1(...)
#endif
#ifdef BUBBLE
#   define BUBBLE1(...) __VA_ARGS__
#else
#   define BUBBLE1(...)
#endif
#ifdef XEMBED
#   define XEMBED1(...) __VA_ARGS__
#else
#   define XEMBED1(...)
#endif

struct tooltip_stuff
{
    unsigned long fg_color, bg_color;
    int y, font_set, visible;
    Font font;
    
    char *space;
    int status, lastchange;
};

struct tiptext
{
    char *text;
    int mallocd;
};

int sock, verbose, sigusr;
struct addrinfo *res;
char *req;
Display *disp;
XImage *img_open, *img_closed, *img_pending, *img_current;
GC gc;
struct json json;
struct tooltip_stuff tstuff;

int pverbose(int ret, const char *format, ...)
{
    if(!verbose)
        return ret;
    
    va_list list;
    va_start(list, format);
    
    if(ret)
        vfprintf(stderr, format, list);
    else
        vprintf(format, list);
    
    va_end(list);
    return ret;
}

void send_ctrl_msg(Window tray, Window win, long msg, long data1, long data2, long data3)
{
    XEvent e;
    
    e.xclient.type = ClientMessage;
    e.xclient.window = win;
    e.xclient.message_type =
        XInternAtom(disp, "_NET_SYSTEM_TRAY_OPCODE", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime;
    e.xclient.data.l[1] = msg;
    e.xclient.data.l[2] = data1;
    e.xclient.data.l[3] = data2;
    e.xclient.data.l[4] = data3;
    
    XSendEvent(disp, tray, False, NoEventMask, &e);
    XSync(disp, False);
}

#ifdef BUBBLE

void send_data_msg(Window tray, Window icon, char *msg, int timeout, int msgid)
{
    XEvent e;
    int len = strlen(msg);
    
    send_ctrl_msg(tray, icon, SYSTEM_TRAY_BEGIN_MESSAGE,
        timeout, len, msgid);

    e.xclient.type = ClientMessage;
    e.xclient.window = icon;
    e.xclient.message_type =
        XInternAtom(disp, "_NET_SYSTEM_TRAY_MESSAGE_DATA", False);
    e.xclient.format = 8;
    
    while(len > 0)
    {
        memcpy(&e.xclient.data, msg, len > 20 ? 20 : len);
        msg += 20;
        len -= 20;
        
        XSendEvent(disp, tray, False, StructureNotifyMask, &e);
        XSync(disp, False);
    }
}

#endif

Window create_icon(Window root, char *argv0)
{
    Window icon;
    XClassHint *hint;
    
    icon = XCreateSimpleWindow(disp, root, 0, 0, 1, 1, 0, 0, 0);
    hint = XAllocClassHint();
    hint->res_name = basename(argv0);
    hint->res_class = PACKAGE;
    XSetClassHint(disp, icon, hint);
    XFree(hint);
    XStoreName(disp, icon, PACKAGE);
    
    XSelectInput(disp, icon, ExposureMask|EnterWindowMask|LeaveWindowMask);
    
    return icon;
}

Window create_tooltip(Window root, int screen)
{
    Window tooltip;
    XSetWindowAttributes attr;
    XGCValues gcval;
    
    attr.override_redirect = 1;
    attr.background_pixel = tstuff.bg_color;
    
    tooltip = XCreateWindow(disp, root, 0, 0, 1, 1, 0, 0, InputOutput,
        CopyFromParent, CWOverrideRedirect|CWBackPixel, &attr);
    
    gcval.foreground = tstuff.fg_color;
    gcval.background = tstuff.bg_color;
    if(tstuff.font_set)
    {
        gcval.font = tstuff.font;
        gc = XCreateGC(disp, tooltip, GCForeground|GCBackground|GCFont, &gcval);
    }
    else
        gc = XCreateGC(disp, tooltip, GCForeground|GCBackground, &gcval);
    
    XSelectInput(disp, tooltip, ExposureMask|VisibilityChangeMask);
    
    return tooltip;
}

void draw_text(Window root, Window tray, Window tooltip, struct tiptext tips[], int n)
{
    int dir, ascent, descent, x, y, height, width, max;
    XCharStruct xchar;
    XWindowAttributes attr_root, attr_tray;
    
    for(x=0,max=0; x<n; x++)
        if((width = strlen(tips[x].text)) > max)
            max = width;
    
    XGetWindowAttributes(disp, root, &attr_root);
    XGetWindowAttributes(disp, tray, &attr_tray);
    
    XQueryTextExtents(disp, XGContextFromGC(gc), tips[0].text,
        max, &dir, &ascent, &descent, &xchar);
    
    xchar.width += BORDER*4;
    x = attr_tray.x+attr_tray.width/2-xchar.width/2;
    height = (ascent+descent)*n+BORDER*2;
    if(x < 0)
        x = 0;
    if(x+xchar.width > attr_root.width)
        x = attr_root.width-xchar.width;
    if(attr_tray.y < attr_root.height/2)
        y = tstuff.y;
    else
        y = attr_root.height - tstuff.y - height;
    
    XMoveResizeWindow(disp, tooltip, x, y, xchar.width, height);
    XRaiseWindow(disp, tooltip);
    XMapWindow(disp, tooltip);
    
    for(x=0; x<n; x++)
    {
        XDrawImageString(disp, tooltip, gc, BORDER*2,
            BORDER+ascent+x*(ascent+descent), tips[x].text,
            strlen(tips[x].text));
        if(tips[x].mallocd)
            free(tips[x].text);
    }
}

void show_tooltip(Window root, Window tray, Window tooltip)
{
    int sec, tipcount = 1;
    struct tiptext tips[2];
    
    memset(tips, 0, 2*sizeof(struct tiptext));
    
    switch(tstuff.status)
    {
    case PENDING:
        tips[0].text = "pending...";
        break;
    case OPEN:
    case CLOSED:
        asprintf(&tips[0].text, "Space: %s", tstuff.space);
        tips[0].mallocd = 1;
        
        sec = difftime(time(0), tstuff.lastchange);
        asprintf(&tips[1].text, "Status: %s for %02i:%02i:%02i",
            tstuff.status==OPEN?"open":"closed",
            sec/3600, (sec/60)%60, sec%60);
        tips[1].mallocd = 1;
        tipcount++;
        break;
    case LOST:
        tips[0].text = "api parsing failed...";
        break;
    }
    
    draw_text(root, tray, tooltip, tips, tipcount);
}

Window dock_tray(int screen, Window icon, Window root)
{
    Window tray;
    char buf[100];
    XEvent e;
    
    sprintf(buf, "_NET_SYSTEM_TRAY_S%i", screen);
    tray = XGetSelectionOwner(disp, XInternAtom(disp, buf, False));
    while(tray == None)
    {
        XSelectInput(disp, root, StructureNotifyMask);
        pverbose(0, "Waiting for Systray\n");
        while(1)
        {
            XNextEvent(disp, &e);
            if(e.xclient.message_type == XInternAtom(disp, "MANAGER", False))
                break;
        }
        tray = XGetSelectionOwner(disp, XInternAtom(disp, buf, False));
    }
    pverbose(0, "docked\n");
    
    XSelectInput(disp, tray, StructureNotifyMask);
    send_ctrl_msg(tray, tray, SYSTEM_TRAY_REQUEST_DOCK, icon, 0, 0);
    
    return tray;
}

int connect_api(int *sock, char *dest, char *port)
{
    struct addrinfo hints, *iter, *res;
    int ret, sockopt;
    
    if((*sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Failed to create socket");
        return 6;
    }
    
    sockopt = 1;
    setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    res_init();
    
    if((ret = getaddrinfo(dest, port, &hints, &res)))
    {
        close(*sock);
        return pverbose(7, "Failed to resolve destination: %s\n", gai_strerror(ret));
    }
    
    for(iter = res; iter; iter = iter->ai_next)
    {
        while((ret = connect(*sock, iter->ai_addr, iter->ai_addrlen)) == -1 && errno == EINTR);
        if(!ret)
            break;
    }
    freeaddrinfo(res);
    if(ret == -1)
    {
        perror("Failed to connect to destination");
        close(*sock);
        return 8;
    }
    return 0;
}

int strpfx(char *str, char *prefix)
{
    while(*str && *prefix && *str == *prefix)
    {
        str++;
        prefix++;
    }
    return *prefix;
}

char* request(int sock, char *host, char *path, char **dst, int *size)
{
    char req[100];
    int count = 0, ret, len = 0;
    char *ptr, *content;
    
    sprintf(req, "GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
    send(sock, req, strlen(req), 0);
    
    while(!len || count < len)
    {
        while((ret = recv(sock, *dst+count, *size-count, 0)) <= 0)
        {
            if(ret == -1)
            {
                if(errno == EINTR)
                    continue;
                perror("Failed to recv");
            }
            return 0;
        }
        count += ret;
        while(count == *size)
        {
            *size *= 2;
            *dst = realloc(*dst, *size);
            while((ret = recv(sock, *dst+count, *size-count, 0)) <= 0)
            {
                if(ret == -1)
                {
                    if(errno == EINTR)
                        continue;
                    perror("Failed to recv");
                }
                return 0;
            }
            count += ret;
        }
        if(!len && (content = strstr(*dst, "\r\n\r\n")))
        {
            content += 4;
            len = content - *dst;
            ptr = strstr(*dst, "Content-Length: ");
            strtok(ptr, "\r\n");
            len += atoi(ptr + strlen("Content-Length: "));
        }
    }
    return content;
}

void sighandler(int signal)
{
    switch(signal)
    {
    case SIGINT:
        close(sock);
        free(req);
        NOTIFY1(notify_uninit());
        json_free(&json);
        XDestroyImage(img_closed);
        XDestroyImage(img_open);
        XDestroyImage(img_pending);
        XFreeGC(disp, gc);
        XCloseDisplay(disp);
        exit(0);
    case SIGUSR1:
        sigusr = 1;
        break;
    }
}

int event_loop(Window root, Window tray, Window icon, Window tooltip, XImage *img, struct pollfd *pfds, int sec)
{
    XEvent e;
    struct timeval now, than;
    int msec, ret;
    sigset_t set, oldset;
    struct timespec delay;
    
    msec = sec*1000;
    than.tv_sec = 0;
    
    while(1)
    {
        gettimeofday(&now, 0);
        if(than.tv_sec)
            msec -= (now.tv_sec-than.tv_sec)*1000
                  + (now.tv_usec-than.tv_usec)/1000;
        than = now;
        
        if(msec <= 0)
            return 0;
        
        delay.tv_sec = msec/1000;
        delay.tv_nsec = (msec%1000)*1000000;
        
        sigfillset(&set);
        sigprocmask(SIG_BLOCK, &set, &oldset);
        
        if(sigusr)
        {
            sigusr = 0;
            sigprocmask(SIG_SETMASK, &oldset, 0);
            pverbose(0, "received SIGUSR1\n");
            return 0;
        }
        
        ret = ppoll(pfds, 1, &delay, &oldset);
        sigprocmask(SIG_SETMASK, &oldset, 0);
        
        switch(ret)
        {
        case -1:
            if(errno == EINTR)
            {
                if(sigusr)
                {
                    sigusr = 0;
                    pverbose(0, "received SIGUSR1\n");
                    return 0;
                }
                continue;
            }
            perror("Failed to poll display");
            return 9;
        case 0:
            return 0;
        default:
            if(pfds[0].revents & POLLIN)
            {
                while(XPending(disp))
                {
                    XNextEvent(disp, &e);
                    switch(e.xany.type)
                    {
                    case Expose: // icon/tooltip
                        if(((XExposeEvent*)&e)->window == icon)
                        {
                            XPutImage(disp, icon, DefaultGC(disp, DefaultScreen(disp)),
                                img, 0, 0, 0, 0, img->width, img->height);
                            XFlush(disp);
                        }
                        else
                            show_tooltip(root, tray, tooltip);
                        break;
                    case EnterNotify: // icon
                        tstuff.visible = 1;
                        show_tooltip(root, tray, tooltip);
                        break;
                    case LeaveNotify: // icon
                        tstuff.visible = 0;
                        XUnmapWindow(disp, tooltip);
                        break;
                    case DestroyNotify: // tray
                        return -1;
                    case VisibilityNotify: // tooltip
                        switch(((XVisibilityEvent*)&e)->state)
                        {
                        case VisibilityPartiallyObscured:
                        case VisibilityFullyObscured:
                            if(tstuff.visible)
                                show_tooltip(root, tray, tooltip);
                            break;
                        }
                        break;
                    }
                }
            }
            else if(pfds[0].revents & POLLHUP)
            {
                fprintf(stderr, "POLLHUP on display\n");
                return 10;
            }
            else if(pfds[0].revents & POLLERR)
            {
                fprintf(stderr, "POLLERR on display\n");
                return 11;
            }
        }
    }
}

void check_num_option(char *arg, char *name)
{
    if(strspn(optarg, "1234567890") != strlen(arg))
    {
        printf("%s option not numeric\n", name);
        exit(1);
    }
}

unsigned long parse_color(int screen, char *arg, char *name)
{
    int len;
    char buf[12];
    XColor color;
    
    if(*arg == '#')
        arg++;
    len = strlen(arg);
    if(len != 6 || strspn(arg, "1234567890abcdefABCDEF") != len)
    {
        printf("%s option no hex RGB value\n", name);
        exit(1);
    }
    sprintf(buf, "rgb:%.2s/%.2s/%.2s", arg, arg+2, arg+4);
    XParseColor(disp, DefaultColormap(disp, screen), buf, &color);
    XAllocColor(disp, DefaultColormap(disp, screen), &color);
    return color.pixel;
}

int parse_api_state(struct json *jstate)
{
    struct json *jp;
    
    if((jp = json_get("{open:b", jstate)))
    {
        switch(jp->v.bool)
        {
        case true:
            return OPEN;
        case false:
            return CLOSED;
        }
    }
    return pverbose(LOST, "json: 'open' not found/malformed\n");
}

int parse_api()
{
    struct json *jp, *state;
    char *ptr;
    int version = 0, status;
    
    if(verbose > 1)
    {
        json_print(&json);
        printf("\n");
    }
    
    if(!(jp = json_get("{api:s", &json)))
        return pverbose(LOST, "json: 'api' not found/malformed\n");
    
    pverbose(0, "api version: %s\n", jp->v.string);
    
    if(jp->v.string[0] == '-')
        jp->v.string++;
    
    if((ptr = strchr(jp->v.string, '.')))
    {
        *ptr = 0;
        if(strspn(ptr+1, "1234567890") != strlen(ptr+1))
            return pverbose(LOST, "json: 'api' malformed\n");
        version = atoi(ptr+1);
    }
    
    if(strspn(jp->v.string, "1234567890") != strlen(jp->v.string))
        return pverbose(LOST, "json: 'api' malformed\n");
    version += atoi(jp->v.string)*100;
    
    if(version > 13)
    {
        pverbose(0, "warning: parsing unsupported api version\n");
        version = 13;
    }
    
    switch(version)
    {
    case 13:
        if((state = json_get("{state:{", &json)))
            break;
        pverbose(LOST, "json: 'state' not found/malformed\n");
    default:
        state = &json;
    }
    
    if((status = parse_api_state(state)) == LOST)
        return status;
    
    if((jp = json_get("{space:s", &json)))
        tstuff.space = jp->v.string;
    else
        pverbose(0, "json: 'space' not found/malformed\n");
    
    if((jp = json_get("{lastchange:i", state)))
        tstuff.lastchange = jp->v.number.l;
    else
        pverbose(0, "json: 'lastchange' not found/malformed\n");
    
    return status;
}

int main(int argc, char *argv[])
{
    // xlib
    Window root, tray, icon, tooltip;
    int screen;
    BUBBLE1(int msgid = 0);
    XEMBED1(unsigned long info[2]);
    
    // args
    int opt;
    int refresh = 5*60;
    char *dest = 0, *port = "80", *path;
    NOTIFYBUBBLE1(int timeout = 3*1000);
    
    // else
    int status = PENDING, sleeptime;
    NOTIFY1(NotifyNotification *notify_open, *notify_closed);
    
    // tmp
    int ret, data_size;
    char buf[100], *ptr, *data;
    char **fonts;
    
    if(!(disp = XOpenDisplay(NULL)))
    {
        fprintf(stderr, "Failed to open display\n");
        return 2;
    }
    
    verbose = 0;
    sigusr = 0;
    screen = DefaultScreen(disp);
    
    memset(&tstuff, 0, sizeof(struct tooltip_stuff));
    tstuff.fg_color = WhitePixel(disp, 0);
    tstuff.bg_color = BlackPixel(disp, 0);
    tstuff.y = 65;
    tstuff.font_set = 0;
    tstuff.visible = 0;
    
    while((opt = getopt(argc, argv, OPTSTR)) != -1)
    {
        switch(opt)
        {
        case 'v':
            verbose = optarg ? strlen(optarg)+1 : 1;
            break;
        case 'r':
            check_num_option(optarg, "refresh");
            refresh = atoi(optarg)*60;
            break;
        case 'p':
            check_num_option(optarg, "port");
            port = optarg;
            break;
        case 'y':
            check_num_option(optarg, "tooltip y position");
            tstuff.y = atoi(optarg);
            break;
        case 'c':
            tstuff.fg_color = parse_color(screen, optarg, "foregound color");
            break;
        case 'b':
            tstuff.bg_color = parse_color(screen, optarg, "backgound color");
            break;
        case 'f':
            fonts = XListFonts(disp, optarg, 1, &ret);
            if(!fonts)
            {
                fprintf(stderr, "No matching font found\n");
                return 1;
            }
            XFreeFontNames(fonts);
            tstuff.font = XLoadFont(disp, optarg);
            tstuff.font_set = 1;
            break;
#if defined NOTIFY || defined BUBBLE
        case 't':
            check_num_option(optarg, "timeout");
            timeout = atoi(optarg)*1000;
            break;
#endif
        }
    }
    
    if(optind == argc)
    {
        printf(USAGE, argv[0]);
        return 1;
    }
    
    dest = argv[optind];
    if(!strpfx(dest, "http://"))
        dest += strlen("http://");
    path = strchr(dest, '/');
    if(!path)
    {
        printf("Path to API JSON missing\n");
        return 1;
    }
    *path++ = 0;
    
    root = RootWindow(disp, screen);
    icon = create_icon(root, argv[0]);
    tooltip = create_tooltip(root, screen);
    
#ifdef XEMBED
    info[0] = 0;
    info[1] = 1;
    XChangeProperty(disp, icon,
        XInternAtom(disp, "_XEMBED_INFO", False),
        XInternAtom(disp, "_XEMBED_INFO", False),
        32, PropModeReplace, (unsigned char*)info, 2);
#endif
    
    tray = dock_tray(screen, icon, root);
    
    memset(buf, 0, 100);
    readlink("/proc/self/exe", buf, 100);
    ptr = strrchr(buf, '/');
    strcpy(ptr+1, "../share/closed.xpm");
    if(XpmReadFileToImage(disp, buf, &img_closed, NULL, NULL))
    {
        fprintf(stderr, "Failed to load closed.xpm\n");
        return 3;
    }
    strcpy(ptr+1, "../share/open.xpm");
    if(XpmReadFileToImage(disp, buf, &img_open, NULL, NULL))
    {
        fprintf(stderr, "Failed to load open.xpm\n");
        return 4;
    }
    strcpy(ptr+1, "../share/pending.xpm");
    if(XpmReadFileToImage(disp, buf, &img_pending, NULL, NULL))
    {
        fprintf(stderr, "Failed to load pending.xpm\n");
        return 5;
    }
    
    sleep(1); // wait until docked before drawing
    
    XPutImage(disp, icon, DefaultGC(disp, screen), img_pending,
        0, 0, 0, 0, img_pending->width, img_pending->height);
    XFlush(disp);
    img_current = img_pending;
    
    data_size = 100;
    req = malloc(data_size);
    json_init(&json);
    
    struct pollfd pfds;
    pfds.fd = ConnectionNumber(disp);
    pfds.events = POLLIN;
    
    signal(SIGINT, sighandler);
    signal(SIGUSR1, sighandler);
    
#ifdef NOTIFY
    notify_init(PACKAGE);
    notify_open = notify_notification_new("space open", 0, "dialog-information");
    notify_closed = notify_notification_new("space closed", 0, "dialog-information");
    notify_notification_set_timeout(notify_open, timeout);
    notify_notification_set_timeout(notify_closed, timeout);
#endif
    
    while(1)
    {
        sleeptime = 60;
        if(connect_api(&sock, dest, port))
        {
            status = PENDING;
            img_current = img_pending;
        }
        else if(!(data = request(sock, dest, path, &req, &data_size)))
        {
            status = PENDING;
            img_current = img_pending;
            close(sock);
        }
        else if(json_parse(data, &json))
        {
malformed:  if(status != LOST)
            {
                status = LOST;
                fprintf(stderr, "json malformed\n");
                img_current = img_pending;
            }
            else
                goto close;
        }
        else switch(parse_api())
        {
        case LOST:
            goto malformed;
        case OPEN:
            if(status != OPEN)
            {
                status = OPEN;
                pverbose(0, "open\n");
                img_current = img_open;
                BUBBLE1(send_data_msg(tray, icon, "space open", timeout, msgid++));
                NOTIFY1(notify_notification_show(notify_open, NULL));
            }
            else
                goto close;
            break;
        case CLOSED:
            if(status != CLOSED)
            {
                status = CLOSED;
                pverbose(0, "closed\n");
                img_current = img_closed;
                BUBBLE1(send_data_msg(tray, icon, "space closed", timeout, msgid++));
                NOTIFY1(notify_notification_show(notify_closed, NULL));
            }
            else
                goto close;
            break;
        }
        
        XPutImage(disp, icon, DefaultGC(disp, screen), img_current,
            0, 0, 0, 0, img_current->width, img_current->height);
        XFlush(disp);
        
close:
        close(sock);
        sleeptime = refresh;
        tstuff.status = status;
        switch((ret = event_loop(root, tray, icon, tooltip, img_current, &pfds, sleeptime)))
        {
        case -1:
            XDestroyWindow(disp, icon);
            icon = create_icon(root, argv[0]);
            tray = dock_tray(screen, icon, root);
            sleep(1);
            XPutImage(disp, icon, DefaultGC(disp, screen), img_pending,
                0, 0, 0, 0, img_pending->width, img_pending->height);
            XFlush(disp);
            status = PENDING;
            break;
        case 0:
            break;
        default:
            return ret;
        }
    }
    
    return 0;
}
