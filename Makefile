
NAME=spacestatus

all: $(NAME)

notify: FLAGS = -D NOTIFY
notify: CFLAGS := $(CFLAGS) $$(pkg-config --cflags --libs libnotify)
notify: $(NAME)

bubble: FLAGS = -D BUBBLE
bubble: $(NAME)

$(NAME): $(NAME).c
	gcc -Wall -ggdb $(NAME).c -o $(NAME) -lX11 -lXpm $(CFLAGS) $(FLAGS)
