PROG = streamCaptureDemo
CC=g++ -O0
CFLAGS32= -m32 -g -Wall -D_FILE_OFFSET_BITS=64
CFLAGS64= -m64 -g -Wall -D_FILE_OFFSET_BITS=64
USER_OBJS =
OBJS= 

all: $(PROG) 

# compiling the source file.
$(PROG) : streamCaptureDemo.cpp
	${CC} ${CFLAGS64} streamCaptureDemo.cpp -o $@ ${OBJS} ${USER_OBJS} -I/vma/src/vma/ -L. -lpthread -lrt  -ldl

clean:
	rm -rf $(PROG)  $(OBJS) 


