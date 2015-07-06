CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lpthread -ljpeg
OBJ = strlcpy.o refresher.o fflag.o tmoffset.o ts.o memstr.o fduxk.o addr.o sleep.o pool.o error.o http.o msg.o main.o ocr.o jpeg.o captcha.o
TARGET = xk

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)
	
$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

clean: 
	rm -f $(TARGET) $(OBJ)

