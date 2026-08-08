FROM ubuntu:20.04 AS build
ARG MODEL
#shell,rtmp,rtsp,rtsps,http,https,rtp
EXPOSE 1935/tcp
EXPOSE 554/tcp
EXPOSE 80/tcp
EXPOSE 443/tcp
EXPOSE 10000/udp
EXPOSE 10000/tcp
EXPOSE 8000/udp
EXPOSE 8000/tcp
EXPOSE 9000/udp

# ADD sources.list /etc/apt/sources.list

RUN apt-get update && \
         DEBIAN_FRONTEND="noninteractive" \
         apt-get install -y --no-install-recommends \
         build-essential \
         cmake \
         git \
         curl \
         vim \
         wget \
         ca-certificates \
         tzdata \
         libssl-dev \
         libsrtp2-dev \
         gcc \
         g++ \
         gdb && \
         apt-get autoremove -y && \
         apt-get clean -y && \
         rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/media
COPY . /opt/media/ZLMediaKit
WORKDIR /opt/media/ZLMediaKit

# 3rdpart init: 拉取子模块(gitee 源)
# 宿主机已 checkout 子模块时, COPY . 已携带源码, 此步仅为兜底; 内网无法访问 gitee 时忽略失败
WORKDIR /opt/media/ZLMediaKit
RUN git submodule update --init --recursive || true

RUN mkdir -p build release/linux/${MODEL}/

WORKDIR /opt/media/ZLMediaKit/build
RUN cmake -DCMAKE_BUILD_TYPE=${MODEL} -DENABLE_WEBRTC=true -DENABLE_SCTP=false -DENABLE_FFMPEG=false -DENABLE_TESTS=false -DENABLE_API=false .. && \
    make -j $(nproc)

FROM ubuntu:20.04
ARG MODEL

# ADD sources.list /etc/apt/sources.list

RUN apt-get update && \
         DEBIAN_FRONTEND="noninteractive" \
         apt-get install -y --no-install-recommends \
         vim \
         wget \
         ca-certificates \
         tzdata \
         curl \
         libssl1.1 \
         libsrtp2 \
         ffmpeg && \
         apt-get autoremove -y && \
         apt-get clean -y && \
    rm -rf /var/lib/apt/lists/*

ENV TZ=Asia/Shanghai
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime \
        && echo $TZ > /etc/timezone && \
        mkdir -p /opt/media/bin/www /opt/media/dump/disk1

WORKDIR /opt/media/bin/
COPY --from=build /opt/media/ZLMediaKit/release/linux/${MODEL}/MediaServer /opt/media/ZLMediaKit/default.pem /opt/media/bin/
# 使用 Linux 配置模板(仓库 config.ini 含 Windows 路径, 不适用于容器)
COPY package/linux/config.ini.linux /opt/media/conf/config.ini
# 模板默认路径为 /opt/zlm_svac, 容器内统一改为 /opt/media
RUN sed -i 's#/opt/zlm_svac#/opt/media#g' /opt/media/conf/config.ini
COPY --from=build /opt/media/ZLMediaKit/www/ /opt/media/bin/www/
ENV PATH /opt/media/bin:$PATH
CMD ["./MediaServer","-s", "default.pem", "-c", "../conf/config.ini", "-l","0"]
