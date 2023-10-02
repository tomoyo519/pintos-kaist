cd threads
make clean
make
cd build
source ../../activate
pintos --gdb -- -q run priority-change
# pintos -- -q run priority-donate-multiple