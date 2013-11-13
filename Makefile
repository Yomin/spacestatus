
NAME=spacestatus

all: $(NAME)

$(NAME): $(NAME).c
	gcc -Wall -ggdb $(NAME).c -o $(NAME) -lX11 -lXpm
