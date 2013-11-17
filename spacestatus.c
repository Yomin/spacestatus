/*
 * Copyright (c) 2013 Martin Rödel aka Yomin
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <libgen.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/xpm.h>

#ifdef NOTIFY
#   include <libnotify/notify.h>
#endif

#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

#if defined NOTIFY || defined BUBBLE
#   define OPTSTR "r:p:t:"
#   define USAGE "Usage: %s [-r <min>] [-p <port>] [-t <sec>] <dest>\n"
#else
#   define OPTSTR "r:p:"
#   define USAGE "Usage: %s [-r <min>] [-p <port>] <dest>\n"
#endif

int sock;
struct addrinfo *res;
char *req;

void send_ctrl_msg(Display *disp, Window tray, Window win, long msg, long data1, long data2, long data3)
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

void send_data_msg(Display *disp, Window tray, Window icon, char *msg, int timeout, int msgid)
{
    XEvent e;
    int len = strlen(msg);
    
    send_ctrl_msg(disp, tray, icon, SYSTEM_TRAY_BEGIN_MESSAGE,
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

Window create_icon(Display *disp, Window root, char *argv0)
{
    Window icon;
    XClassHint *hint;
    
    icon = XCreateSimpleWindow(disp, root, 0, 0, 1, 1, 0, 0, 0);
    hint = XAllocClassHint();
    hint->res_name = basename(argv0);
    hint->res_class = "spacestatus";
    XSetClassHint(disp, icon, hint);
    XFree(hint);
    XStoreName(disp, icon, "spacestatus");
    
    return icon;
}

Window dock_tray(Display *disp, int screen, Window icon, Window root)
{
    Window tray;
    char buf[100];
    XEvent e;
    
    sprintf(buf, "_NET_SYSTEM_TRAY_S%i", screen);
    tray = XGetSelectionOwner(disp, XInternAtom(disp, buf, False));
    while(tray == None)
    {
        XSelectInput(disp, root, StructureNotifyMask);
        printf("Waiting for Systray\n");
        while(1)
        {
            XNextEvent(disp, &e);
            if(e.xclient.message_type == XInternAtom(disp, "MANAGER", False))
                break;
        }
        tray = XGetSelectionOwner(disp, XInternAtom(disp, buf, False));
    }
    printf("docked\n");
    
    XSelectInput(disp, icon, ExposureMask);
    XSelectInput(disp, tray, StructureNotifyMask);
    send_ctrl_msg(disp, tray, tray, SYSTEM_TRAY_REQUEST_DOCK, icon, 0, 0);
    
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
    
    if((ret = getaddrinfo(dest, port, &hints, &res)))
    {
        printf("Failed to resolve destination: %s\n", gai_strerror(ret));
        close(*sock);
        return 7;
    }
    
    for(iter = res; iter; iter = iter->ai_next)
    {
        ret = connect(*sock, iter->ai_addr, iter->ai_addrlen);
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

char* request(int sock, char *host, char **dst, int *size)
{
    char req[100];
    int count = 0, ret, len = 0;
    char *ptr, *content;
    
    sprintf(req, "GET /json HTTP/1.0\r\nHost: %s\r\n\r\n", host);
    send(sock, req, strlen(req), 0);
    
    while(!len || count < len)
    {
        ret = recv(sock, *dst+count, *size-count, 0);
        if(ret == -1)
        {
            perror("Failed to recv");
            return 0;
        }
        count += ret;
        while(count == *size)
        {
            *size *= 2;
            *dst = realloc(*dst, *size);
            ret = recv(sock, *dst+count, *size-count, 0);
            if(ret == -1)
            {
                perror("Failed to recv");
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

void cleanup(int signal)
{
    close(sock);
    free(req);
#ifdef NOTIFY
    notify_uninit();
#endif
    exit(0);
}

int event_loop(Display *disp, Window icon, XImage *img, struct pollfd *pfds, int sec)
{
    XEvent e;
    struct timeval now, than;
    int msec = sec*1000;
    
    than.tv_sec = 0;
    
    while(1)
    {
        gettimeofday(&now, 0);
        if(than.tv_sec)
            msec -= (now.tv_sec-than.tv_sec)*1000
                  + (now.tv_usec-than.tv_usec)/1000;
        than = now;
        
        switch(poll(pfds, 1, msec))
        {
        case -1:
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
                    case Expose: // icon
                        XPutImage(disp, icon, DefaultGC(disp, 0),
                            img, 0, 0, 0, 0, img->width, img->height);
                        XFlush(disp);
                        break;
                    case DestroyNotify: // tray
                        return -1;
                    }
                }
            }
            else if(pfds[0].revents & POLLHUP)
            {
                printf("POLLHUP on display\n");
                return 10;
            }
            else if(pfds[0].revents & POLLERR)
            {
                printf("POLLERR on display\n");
                return 11;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    Display *disp;
    Window root, icon;
    int screen;
    char buf[100], *ptr;
    XImage *img_open, *img_closed, *img_current;
    
#ifdef BUBBLE
    int msgid = 0;
    Window tray;
#endif
#ifdef XEMBED
    unsigned long info[2];
#endif
    
    int opt;
    int refresh = 5*60;
    char *dest = 0, *port = "80";
#if defined NOTIFY || defined BUBBLE
    int timeout = 3*1000;
#endif
#ifdef NOTIFY
    NotifyNotification *notify_open, *notify_closed;
#endif
    
    char *json;
    int size;
    
    int open = 0, ret, sleeptime;
    
    while((opt = getopt(argc, argv, OPTSTR)) != -1)
    {
        switch(opt)
        {
        case 'r':
            if(strspn(optarg, "1234567890") != strlen(optarg))
            {
                printf("refresh option not numeric\n");
                return 1;
            }
            refresh = atoi(optarg)*60;
            break;
        case 'p':
            if(strspn(optarg, "1234567890") != strlen(optarg))
            {
                printf("port option not numeric\n");
                return 1;
            }
            port = optarg;
            break;
#if defined NOTIFY || defined BUBBLE
        case 't':
            if(strspn(optarg, "1234567890") != strlen(optarg))
            {
                printf("timeout option not numeric\n");
                return 1;
            }
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
    
    if(!(disp = XOpenDisplay(NULL)))
    {
        printf("Failed to open display\n");
        return 2;
    }
    
    screen = DefaultScreen(disp);
    
    root = RootWindow(disp, screen);
    icon = create_icon(disp, root, argv[0]);
    
#ifdef XEMBED
    info[0] = 0;
    info[1] = 1;
    XChangeProperty(disp, icon,
        XInternAtom(disp, "_XEMBED_INFO", False),
        XInternAtom(disp, "_XEMBED_INFO", False),
        32, PropModeReplace, (unsigned char*)info, 2);
#endif
    
#ifdef BUBBLE
    tray = dock_tray(disp, screen, icon, root);
#else
    dock_tray(disp, screen, icon, root);
#endif
    
    readlink("/proc/self/exe", buf, 100);
    ptr = strrchr(buf, '/');
    strcpy(ptr+1, "closed.xpm");
    if(XpmReadFileToImage(disp, buf, &img_closed, NULL, NULL))
    {
        printf("Failed to load closed.xpm\n");
        return 4;
    }
    strcpy(ptr+1, "open.xpm");
    if(XpmReadFileToImage(disp, buf, &img_open, NULL, NULL))
    {
        printf("Failed to load open.xpm\n");
        return 5;
    }
    
    sleep(1); // wait until docked before drawing
    
    XPutImage(disp, icon, DefaultGC(disp, 0), img_closed,
        0, 0, 0, 0, img_closed->width, img_closed->height);
    XFlush(disp);
    img_current = img_closed;
    
    size = 100;
    req = malloc(size);
    
    struct pollfd pfds;
    pfds.fd = ConnectionNumber(disp);
    pfds.events = POLLIN;
    
    signal(SIGINT, cleanup);
    
#ifdef NOTIFY
    notify_init("spacestatus");
    notify_open = notify_notification_new("space open", 0, "dialog-information");
    notify_closed = notify_notification_new("space closed", 0, "dialog-information");
    notify_notification_set_timeout(notify_open, timeout);
    notify_notification_set_timeout(notify_closed, timeout);
#endif
    
    while(1)
    {
        sleeptime = 60;
        if(connect_api(&sock, dest, port))
            goto sleep;
        if(!(json = request(sock, dest, &req, &size)))
        {
            close(sock);
            goto sleep;
        }
        
        if(strstr(json, "\"open\":true"))
        {
            if(!open)
            {
                open = 1;
                printf("open\n");
                XPutImage(disp, icon, DefaultGC(disp, 0), img_open,
                    0, 0, 0, 0, img_open->width, img_open->height);
                img_current = img_open;
#ifdef BUBBLE
                send_data_msg(disp, tray, icon, "space open", timeout, msgid++);
#endif
#ifdef NOTIFY
                notify_notification_show(notify_open, NULL);
#endif
            }
        }
        else
        {
            if(open)
            {
                open = 0;
                printf("closed\n");
                XPutImage(disp, icon, DefaultGC(disp, 0), img_closed,
                    0, 0, 0, 0, img_closed->width, img_closed->height);
                img_current = img_closed;
#ifdef BUBBLE
                send_data_msg(disp, tray, icon, "space closed", timeout, msgid++);
#endif
#ifdef NOTIFY
                notify_notification_show(notify_closed, NULL);
#endif
            }
        }
        XFlush(disp);
        
        close(sock);
        sleeptime = refresh;
sleep:
        switch((ret = event_loop(disp, icon, img_current, &pfds, sleeptime)))
        {
        case -1:
            XDestroyWindow(disp, icon);
            icon = create_icon(disp, root, argv[0]);
#ifdef BUBBLE
            tray = dock_tray(disp, screen, icon, root);
#else
            dock_tray(disp, screen, icon, root);
#endif
            sleep(1);
            XPutImage(disp, icon, DefaultGC(disp, 0), img_current,
                0, 0, 0, 0, img_current->width, img_current->height);
            XFlush(disp);
            break;
        case 0:
            break;
        default:
            return ret;
        }
    }
    
    return 0;
}
