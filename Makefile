CFLAGS=--std=c99 `freetype-config --cflags` -Wextra -pedantic -Werror
LDFLAGS=-lm `freetype-config --libs`
TARGET=makefont

all:
	@$(CC) $(CFLAGS) *.c $(LDFLAGS) -o $(TARGET)

clean:
	@rm -f $(TARGET)
