CC = gcc
TARGET = server

SRC = $(shell find . -name "*.c")
OBJ = $(patsubst %.c, %.o, $(SRC))

$(TARGET):$(OBJ)
	$(CC) -o $(TARGET) $(OBJ) -lpthread

%.o:%.c
	$(CC) -c $< -o $@

clean:
	$(RM) *.o
