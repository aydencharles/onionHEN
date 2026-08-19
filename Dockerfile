FROM ubuntu:24.04

ARG HOST_UID=1000
ARG HOST_GID=1000

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

RUN if getent group "${HOST_GID}" >/dev/null; then \
            builder_group="$(getent group "${HOST_GID}" | cut -d: -f1)"; \
        else \
            groupadd --gid "${HOST_GID}" builder; \
            builder_group=builder; \
        fi && \
        existing_user="$(getent passwd "${HOST_UID}" | cut -d: -f1 || true)" && \
        if [ -n "${existing_user}" ] && [ "${existing_user}" != builder ]; then \
            usermod --login builder "${existing_user}" && \
            usermod --home /home/builder --move-home builder; \
        elif ! id builder >/dev/null 2>&1; then \
            useradd --uid "${HOST_UID}" --gid "${HOST_GID}" --create-home --shell /bin/bash builder; \
        fi && \
        usermod --gid "${HOST_GID}" --shell /bin/bash builder && \
        chown -R builder:"${builder_group}" /workspace

USER builder
    CMD ["/bin/bash", "-lc", "set -euo pipefail; rm -rf /tmp/onionhen-build; cp -a /workspace/. /tmp/onionhen-build; python3 -c \"from pathlib import Path; files=list(Path('/tmp/onionhen-build').rglob('*.sh'))+list(Path('/tmp/onionhen-build').rglob('*.bash')); [p.write_bytes(p.read_bytes().replace(bytes([13]), bytes())) for p in files]\"; git -C /tmp/onionhen-build submodule update --init --recursive; cd /tmp/onionhen-build; export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk; export PATH=/usr/lib/llvm-18/bin:${PS5_PAYLOAD_SDK}/bin:${PATH}; export LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config; ./scripts/build.sh --jobs 8; rm -rf /workspace/build; cp -a /tmp/onionhen-build/build /workspace/build"]
