FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config libx11-dev libxext-dev libxft-dev libxrandr-dev \
    xvfb xterm xdotool x11-utils x11-apps imagemagick fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
