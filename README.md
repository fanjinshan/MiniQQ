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
