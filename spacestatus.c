
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/xpm.h>

#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

int sock;
struct addrinfo *res;
char *req;

void send_msg(Display *disp, Window tray, long msg, long data1, long data2, long data3)
{
    XEvent e;
    
    e.xclient.type = ClientMessage;
    e.xclient.window = tray;
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
        return 7;
    }
    
    for(iter = res; iter; iter = iter->ai_next)
    {
        ret = connect(*sock, iter->ai_addr, iter->ai_addrlen);
        if(!ret)
            break;
    }
    if(ret == -1)
    {
        perror("Failed to connect to destination");
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
    freeaddrinfo(res);
    exit(0);
}

int main(int argc, char *argv[])
{
    Display *disp;
    Window root, tray, icon;
//    unsigned long info[2];
    int screen;
    char buf[100], *ptr;
    XImage *open, *closed;
    
    int opt;
    int refresh = 5*60;
    char *dest = 0, *port = "80";
    
    char *json;
    int size;
    
    int ret;
    
    while((opt = getopt(argc, argv, "r:h")) != -1)
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
        }
    }
    
    if(optind == argc)
    {
        printf("Usage: %s [-r <min>] [-p <port>] <dest>\n", argv[0]);
        return 1;
    }
    
    dest = argv[optind];
    
    if(!(disp = XOpenDisplay(NULL)))
    {
        printf("Failed to open display\n");
        return 2;
    }
    
    screen = DefaultScreen(disp);
    sprintf(buf, "_NET_SYSTEM_TRAY_S%i", screen);
    
    tray = XGetSelectionOwner(disp,
        XInternAtom(disp, buf, False));
    if(tray == None)
    {
        printf("No systemtray available\n");
        return 3;
    }
    
    root = RootWindow(disp, screen);
    icon = XCreateSimpleWindow(disp, root, 0, 0, 1, 1, 0, 0, 0);
    
    send_msg(disp, tray, SYSTEM_TRAY_REQUEST_DOCK, icon, 0, 0);
    
    readlink("/proc/self/exe", buf, 100);
    ptr = strrchr(buf, '/');
    strcpy(ptr+1, "closed.xpm");
    if(XpmReadFileToImage(disp, buf, &closed, NULL, NULL))
    {
        printf("Failed to load closed.xpm\n");
        return 4;
    }
    strcpy(ptr+1, "open.xpm");
    if(XpmReadFileToImage(disp, buf, &open, NULL, NULL))
    {
        printf("Failed to load open.xpm\n");
        return 5;
    }
    
    /*
    info[0] = 0;
    info[1] = 1;
    XChangeProperty(disp, icon,
        XInternAtom(disp, "_XEMBED_INFO", False),
        XInternAtom(disp, "_XEMBED_INFO", False),
        32, PropModeReplace, (unsigned char*)info, 2);
    */
    
    if((ret = connect_api(&sock, dest, port)))
        return ret;
    
    size = 100;
    req = malloc(size);
    
    if(!(json = request(sock, dest, &req, &size)))
    {
        perror("Failed to recv data");
        return 9;
    }
    
    signal(SIGINT, cleanup);
    
    while(1)
    {
        if(strstr(json, "\"open\":true"))
        {
            printf("open\n");
            XPutImage(disp, icon, DefaultGC(disp, 0), open, 0, 0, 0, 0, open->width, open->height);
        }
        else
        {
            printf("closed\n");
            XPutImage(disp, icon, DefaultGC(disp, 0), closed, 0, 0, 0, 0, closed->width, closed->height);
        }
        XFlush(disp);
        
        close(sock);
        sleep(refresh);
        
        while(1)
        {
            while(connect_api(&sock, dest, port))
                sleep(60);
            if((json = request(sock, dest, &req, &size)))
                break;
            close(sock);
            sleep(60);
        }
    }
    
    return 0;
}
