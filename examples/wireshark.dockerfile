FROM conffuzz-dev:latest

RUN apt update
RUN apt install -y unzip curl wrk
RUN apt install -y flex bison perl python3 cmake ninja-build

# Build zlib
RUN wget https://www.zlib.net/zlib-1.2.12.tar.gz
RUN tar -xf zlib-1.2.12.tar.gz
RUN cd zlib-1.2.12 && CFLAGS="-g -O0" ./configure && make install

# Build libpcap
RUN wget https://www.tcpdump.org/release/libpcap-1.10.1.tar.gz 
RUN tar -zxf libpcap-1.10.1.tar.gz
RUN cd libpcap-1.10.1 && CFLAGS="-g -O0" ./configure && make && make install

# Build wireshark
RUN wget https://www.wireshark.org/download/src/wireshark-3.4.12.tar.xz
RUN tar -xf wireshark-3.4.12.tar.xz
RUN yes | ./wireshark-3.4.12/tools/debian-setup.sh
RUN mkdir wireshark_build
RUN cd wireshark_build && cmake -DBUILD_wireshark=OFF -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=true ../wireshark-3.4.12 && make -j

WORKDIR /root
