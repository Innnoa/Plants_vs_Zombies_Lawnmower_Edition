# syntax=docker/dockerfile:1
FROM debian:12-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

# 基础编译环境与依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    curl \
    ca-certificates \
    protobuf-compiler \
    libprotobuf-dev \
    libasio-dev \
    libspdlog-dev \
 && rm -rf /var/lib/apt/lists/*

# 编译安装 Abseil（Debian 包未提供所需的内部日志库）
ARG ABSL_VERSION=20230802.1
RUN curl -L "https://github.com/abseil/abseil-cpp/archive/refs/tags/${ABSL_VERSION}.tar.gz" \
    | tar xz -C /tmp \
 && cmake -S /tmp/abseil-cpp-${ABSL_VERSION} -B /tmp/absl-build \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -DABSL_PROPAGATE_CXX_STD=ON \
 && cmake --build /tmp/absl-build -j"$(nproc)" \
 && cmake --install /tmp/absl-build \
 && rm -rf /tmp/abseil-cpp-${ABSL_VERSION} /tmp/absl-build

WORKDIR /app/server

# 仅拷贝服务端源码与配置，避免携带客户端资源
COPY server /app/server
# 移除可能由其他版本 protoc 生成的旧文件，确保使用容器内 protoc 重新生成
RUN rm -rf /app/server/generated/*

# 构建 Release 版本
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)"

# 运行阶段
FROM debian:12-slim AS runtime
ENV DEBIAN_FRONTEND=noninteractive

# 运行时需要的依赖（头文件库仍需装 dev 包）
RUN apt-get update && apt-get install -y --no-install-recommends \
    libprotobuf-dev \
    libasio-dev \
    libspdlog-dev \
 && rm -rf /var/lib/apt/lists/*

# 拷贝 Abseil 运行时库
COPY --from=build /usr/local /usr/local

WORKDIR /opt/lawnmower

COPY --from=build /app/server/build/server /opt/lawnmower/server
COPY --from=build /app/server/config /opt/lawnmower/config

EXPOSE 7777 7778

ENTRYPOINT ["./server"]
