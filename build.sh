g++ -O0 -m64 -g -Wall -D_FILE_OFFSET_BITS=64 streamCaptureDemo.cpp -o streamCaptureDemo   -I$1/src/vma -L. -lpthread -lrt  -ldl
