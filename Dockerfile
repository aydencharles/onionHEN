FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    PATH="/usr/local/bin:/usr/lib/llvm-18/bin:/opt/ps5-payload-sdk/bin:${PATH}" \
    PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk \
    KEYSTONE_PREFIX=/usr/local \
    LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config

RUN apt-get update && apt-get install -y --no-install-recommends \
      bash \
      build-essential \
      ca-certificates \
      clang-18 \
      cmake \
      curl \
      lld-18 \
      llvm-18 \
      make \
      meson \
      ninja-build \
      pkg-config \
      python3 \
      python3-pyelftools \
      socat \
      unzip \
      wget \
      xz-utils \
      libssl-dev \
    librsvg2-bin \
            git \
    && ln -sf /usr/lib/llvm-18/bin/llvm-config /usr/local/bin/llvm-config && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace

RUN find /workspace -type f \( -name "*.sh" -o -name "*.bash" \) -print0 | xargs -0 -r sed -i 's/\r$//' && \
    git -C /workspace submodule update --init --recursive && \
    git clone --depth 1 https://github.com/ps5-payload-dev/sdk.git /tmp/ps5-payload-sdk-src && \
    export LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config && \
    export PATH=/usr/lib/llvm-18/bin:${PATH} && \
    make -C /tmp/ps5-payload-sdk-src DESTDIR=/opt/ps5-payload-sdk install && \
    cd /tmp/ps5-payload-sdk-src && \
    export LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config && \
    ./libcxx.sh && \
    cd /workspace && \
    rm -rf /tmp/ps5-payload-sdk-src

CMD ["/bin/bash", "-lc", "find /workspace -type f \\( -name '*.sh' -o -name '*.bash' \\) -print0 | xargs -0 -r sed -i 's/\\r$//' && export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk && export PATH=/usr/lib/llvm-18/bin:${PS5_PAYLOAD_SDK}/bin:${PATH} && export LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config && git -C /workspace submodule update --init --recursive && ./scripts/build.sh --jobs 8"]
