cd threads
make clean
make
cd build
source ../../activate
# pintos --gdb -- -q run priority-donate-multiple
pintos -- -q run priority-donate-multiple