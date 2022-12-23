FROM conffuzz-dev:latest

# Build ffmpeg
# guide from https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu
WORKDIR /root
RUN apt update -qq
RUN apt -y install \
  autoconf \
  automake \
  build-essential \
  cmake \
  git-core \
  libass-dev \
  libfreetype6-dev \
  libgnutls28-dev \
  libmp3lame-dev \
  libsdl2-dev \
  libtool \
  libva-dev \
  libvdpau-dev \
  libvorbis-dev \
  libxcb1-dev \
  libxcb-shm0-dev \
  libxcb-xfixes0-dev \
  meson \
  ninja-build \
  pkg-config \
  texinfo \
  wget \
  yasm \
  zlib1g-dev
RUN wget https://ffmpeg.org/releases/ffmpeg-5.0.tar.gz
RUN tar -xf ffmpeg-5.0.tar.gz
RUN cd ffmpeg-5.0 && ./configure \
  --extra-cflags="-fPIC -fsanitize=address" \
  --extra-ldflags="-fsanitize=address" \
  --enable-shared \
  --disable-optimizations \
  --enable-debug=3 \
  --enable-pic \
  --disable-stripping \
  --disable-asm \
  --disable-static \
  --enable-memory-poisoning \
  --enable-gpl \
  --enable-gnutls \
  --enable-libass \
  --enable-libfreetype \
  --enable-libmp3lame \
  --enable-libvorbis \
  --enable-nonfree
RUN cd ffmpeg-5.0 && make -j && make install
ENV LD_LIBRARY_PATH="/lib:/usr/lib:/usr/local/lib"

# get sample file that we can use to test ffmpeg
RUN wget https://filesamples.com/samples/video/avi/sample_960x540.avi
RUN mv sample_960x540.avi foo.avi

WORKDIR /root
