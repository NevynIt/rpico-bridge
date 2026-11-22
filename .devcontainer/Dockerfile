# syntax=docker/dockerfile:1
FROM debian:bookworm-slim

ARG ARM_GCC_VERSION=15:12.2.rel1-1
ARG PICO_SDK_REF=2.2.0
ARG PICOTOOL_REF=2.2.0
ARG PICO_PIO_USB_REF=0.7.3

ENV PICO_SDK_PATH=/opt/pico/pico-sdk \
    PICO_PIO_USB_PATH=/opt/pico/Pico-PIO-USB \
    PICO_TINYUSB_PATH=/opt/pico/tinyusb-0.21.0

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash build-essential ca-certificates cmake curl file git gh ninja-build \
        pkg-config python3 python3-venv nodejs npm unzip xz-utils bubblewrap \
        "gcc-arm-none-eabi=${ARM_GCC_VERSION}" binutils-arm-none-eabi \
        libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
        usbutils libusb-1.0-0-dev gdb-multiarch openocd \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch "$PICO_SDK_REF" \
        https://github.com/raspberrypi/pico-sdk.git "$PICO_SDK_PATH" \
    && git -C "$PICO_SDK_PATH" submodule update --init --depth 1 \
    && git clone --depth 1 --branch "$PICO_PIO_USB_REF" \
        https://github.com/sekigon-gonnoc/Pico-PIO-USB.git "$PICO_PIO_USB_PATH" \
    && curl -fsSL https://codeload.github.com/hathach/tinyusb/tar.gz/refs/tags/0.21.0 -o /tmp/tinyusb.tar.gz \
    && printf '%s\n' 'b7cdf35c5ccefb0f61640aff94a732ab4ebcb21498201e4a161545891b23ba01  /tmp/tinyusb.tar.gz' | sha256sum -c - \
    && tar -xzf /tmp/tinyusb.tar.gz -C /opt/pico \
    && rm /tmp/tinyusb.tar.gz \
    && git clone --depth 1 --branch "$PICOTOOL_REF" \
        https://github.com/raspberrypi/picotool.git /tmp/picotool \
    && cmake -S /tmp/picotool -B /tmp/picotool/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$PICO_SDK_PATH" \
    && cmake --build /tmp/picotool/build --parallel 2 \
    && cmake --install /tmp/picotool/build \
    && rm -rf /tmp/picotool \
    && picotool version \
    && arm-none-eabi-gcc --version

RUN groupadd --gid 1000 pico \
    && useradd --uid 1000 --gid pico --create-home --shell /bin/bash pico \
    && install -d -m 0700 -o pico -g pico /home/pico/.codex /home/pico/.config/gh \
    && install -d -o pico -g pico /workspace /opt/codex

COPY --chown=pico:pico .devcontainer/codex.toml /home/pico/.codex/config.toml

RUN apt-get update \
    && apt-get install -y --no-install-recommends openssh-client \
    && rm -rf /var/lib/apt/lists/*

ENV HOME=/home/pico \
    CODEX_HOME=/home/pico/.codex \
    GH_CONFIG_DIR=/home/pico/.config/gh
ENV PATH=/opt/codex/bin:$PATH

USER pico
RUN curl -fsSL https://chatgpt.com/codex/install.sh -o /tmp/codex-install.sh \
    && CODEX_HOME=/opt/codex CODEX_INSTALL_DIR=/opt/codex/bin \
        CODEX_NON_INTERACTIVE=1 sh /tmp/codex-install.sh \
    && rm /tmp/codex-install.sh \
    && codex --version

WORKDIR /workspace

USER root
ARG TARGETARCH
RUN case "$TARGETARCH" in \
        amd64) asset=herdr-linux-x86_64; checksum=14d9e8bc8b4a9e0118d77c32266f76d461f09b40342eb23752960bd89f59929d ;; \
        arm64) asset=herdr-linux-aarch64; checksum=17d7536f2da50859523680ef279dffcac8e4a235f4e46fe8da6c2ec44141897c ;; \
        *) printf 'Unsupported Herdr architecture: %s\n' "$TARGETARCH" >&2; exit 1 ;; \
    esac \
    && curl -fsSL "https://github.com/herdrdev/herdr/releases/download/v0.9.5/$asset" -o /tmp/herdr \
    && printf '%s  /tmp/herdr\n' "$checksum" | sha256sum -c - \
    && install -m 0755 /tmp/herdr /usr/local/bin/herdr \
    && install -d -o pico -g pico /home/pico/.config/herdr \
    && rm /tmp/herdr

USER pico
RUN herdr --version

CMD ["bash"]
