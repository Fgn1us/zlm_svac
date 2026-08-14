 流程总览

  产出 Linux 二进制 → 拷到目标机 → 安装+systemd → 验证

  ---
  路径 A：目标机直接编译安装（推荐）

  适用：目标机能联网（或内网有 apt/yum 软件源），能装编译依赖。

  # 1. 把源码放到目标机（任选）
  #    git clone <你的仓库> && cd zlm_svac && git submodule update --init --recursive
  #    或 SCP 整个目录（确认 3rdpart/ZLToolKit/src 有源码）

  # 2. 编译（自动装依赖 + submodule + cmake，产物在 release/linux/Release/）
  chmod +x package/linux/*.sh
  ./package/linux/build_linux.sh

  # 3. 打包
  ./package/linux/pack_linux.sh -v 1.0.0      # 生成 zlm-svac_linux_x86_64_1.0.0.tar.gz

  # 4. 安装 + 启动（tar 包、install_linux.sh、zlm-svac.service 三个文件要在同一目录）
  ./package/linux/install_linux.sh zlm-svac_linux_x86_64_1.0.0.tar.gz

  ---
  路径 B：Windows 上用 Docker 产出 tar，再拷到内网目标机

  适用：目标机纯内网、只有运行环境（无编译工具链/软件源）。

  # ——在 Windows（Docker Desktop + git bash），项目根目录 ——
  docker build --network=host --build-arg MODEL=Release -t zlm-svac-build .

  # 从镜像取出编译产物到仓库 release/linux/Release/
  mkdir -p release/linux
  id=$(docker create zlm-svac-build)
  docker cp "$id":/opt/media/ZLMediaKit/release/linux/Release ./release/linux/Release
  docker rm "$id"

  ./package/linux/pack_linux.sh -v 1.0.0       # 本地打出 tar

  # 把 3 个文件拷到目标机: zlm-svac_linux_x86_64_1.0.0.tar.gz + package/linux/{install_linux.sh,zlm-svac.service}

  # ——在目标机上 ——
  chmod +x install_linux.sh
  ./install_linux.sh zlm-svac_linux_x86_64_1.0.0.tar.gz

  ▎ 路径 B 的前提：dockerfile 已修复，构建时能访问 gitee 拉 ZLToolKit 子模块（git submodule update 有 || true
  ▎ 兜底，宿主机本地已 checkout 的话容器会直接带上源码，更稳）。

  ---
  部署后验证

  systemctl status zlm-svac                 # 服务运行正常
  journalctl -u zlm-svac -f                  # 看日志，无异常
  ss -tlnp | grep -E '554|1935|80|10000'    # 端口监听
  # 推一路 SVAC PS 流(PT 96)验证透传; dump 落盘到 /opt/zlm_svac/dump/disk1/

  ---