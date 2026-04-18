MiniQQ - 基于 LVGL 的即时通讯应用
License Platform LVGL

MiniQQ 是一个基于 LVGL (Light and Versatile Graphics Library) 图形库开发（模拟器）的嵌入式即时通讯演示项目。本项目包含客户端 (app_client) 和服务端 (app_server) 两部分，旨在展示如何在 Linux 平台上实现流畅的 GUI 交互和网络通信。

📂 项目结构
text
app_sdk/
├── app_client/          # [核心] 客户端应用程序源码 (GUI + 逻辑)
├── app_server/          # [核心] 服务端应用程序源码 (消息转发/处理)
├── lvgl/                # [依赖] LVGL 图形库源码
├── libcurl/             # [依赖] Libcurl 网络库源码
├── common/              # [共享] 公共头文件、工具类或协议定义
├── docs/                # 文档与资源
├── .gitignore           # Git 忽略配置
└── README.md            # 项目说明文档

🚀 主要功能
图形界面 (GUI)：基于 LVGL 实现的现代化用户界面。
网络通信：使用 libcurl 进行 HTTP/WebSocket 通信，实现消息收发。
平台支持：支持在 PC Linux 上模拟运行。
模块化设计：客户端与服务端分离，便于独立开发和调试。
🛠️ 前置要求
在编译之前，请确保您的开发环境满足以下条件：

操作系统: Ubuntu 18.04+ 
编译器: GCC >= 7.0 或 Clang >= 10.0 (交叉编译需安装对应架构的工具链)
构建工具: CMake >= 3.10, Make
依赖库:
libpng, libjpeg, freetype (LVGL 所需图像/字体支持)
zlib, openssl (Libcurl 所需)
📦 编译与运行
1. 获取代码
bash
git clone https://github.com/fanjinshan/MiniQQ.git
cd MiniQQ
2. 初始化依赖
如果 lvgl 和 libcurl 是作为 Git Submodule 管理的，请执行：

bash
git submodule update --init --recursive

3. 编译 App Client (客户端)
bash
mkdir -p build_client && cd build_client

# 配置项目 (如果是交叉编译，请添加 -DCMAKE_TOOLCHAIN_FILE=...)
cmake ../app_client

# 编译
make -j$(nproc)

# 运行
./app_client
4. 编译 App Server (服务端)
bash
mkdir -p build_server && cd build_server

# 配置项目
cmake ../app_server

# 编译
make -j$(nproc)

# 运行
./app_server

⚙️ 配置说明
项目使用环境变量或配置文件进行管理。首次运行前，建议复制示例配置：

bash
cp app_client/.env.example app_client/.env
cp app_server/.env.example app_server/.env
编辑 .env 文件以设置：

SERVER_IP: 服务端 IP 地址
SERVER_PORT: 服务端监听端口
LVGL_DISP_WIDTH/HEIGHT: 屏幕分辨率

##部分功能

主界面
<img width="1196" height="727" alt="QQ_1776513374905" src="https://github.com/user-attachments/assets/1f5e1c57-b0c3-49f0-a3d7-e956dc83c5b9" />

私聊
<img width="1833" height="730" alt="QQ_1776513506079" src="https://github.com/user-attachments/assets/a5370038-895b-4bdf-8ca2-9a3a631bafb6" />

AI助手
<img width="1191" height="733" alt="QQ_1776513656405" src="https://github.com/user-attachments/assets/2ba0398a-f834-404f-b663-402966030533" />


# README

## 环境要求

```
cmake >= 3.15
如果本地查询cmake --version小于改版本，可以按下方方法升级指定版本
$ wget http://www.cmake.org/files/v3.15/cmake-3.15.3.tar.gz
$ tar -xvzf cmake-3.15.3.tar.gz
$ cd cmake-3.15.3
$ ./configure
$ make
$ sudo make install
$ cmake --version

注意：使用linux仿真时，需要先安装环境依赖
sudo apt-get install build-essential libsdl2-dev -y

```

## 编译说明

```
1、指定交叉编译工具链工具链库位置
修改build.sh中toolchain_path的位置，改为你本机路径

2、编译相关
编译linux应用
./build.sh -linux
删除编译信息
./build.sh -clean

如果需要配置CMakeLists和屏幕分辨率相关参数，需要执行./build.sh -clean后再重新编译
如果需要切换板卡和PC机应用编译，需要执行./build.sh -clean后再重新编译

3、编译完成后，生成的应用在
build/app/demo

4、推到设备端运行即可
adb push platform/t113/lib/* /usr/lib/  #仅第一次需要push，不修改无需重新push
adb push build/app/res/* /usr/res/      #仅第一次需要push，不修改无需重新push
adb push build/app/demo /usr/bin/

 vi /etc/init.d/rc.final 
 ./usr/bin/demo & 
修改后，记得保存，最好用reboot重启确保可以完全写入


<img width="811" height="493" alt="QQ_1776513066416" src="https://github.com/user-attachments/assets/2693d304-d149-4e89-81e0-48882926da55" />

```
