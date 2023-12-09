CC = gcc
TARGET = client

SRC = $(shell find . -name "*.c")
OBJ = $(patsubst %.c, %.o, $(SRC))

$(TARGET):$(OBJ)
	$(CC) -o $(TARGET) $(OBJ)

%.o:%.c
	$(CC) -c $< -o $@

clean:
	$(RM) *.o
