#ifndef __RES_CONF_H
#define __RES_CONF_H

#ifdef SIMULATOR_LINUX
    #define FONT_PATH "/home/zhb/T113/app_sdk/build/app_client/res/font/"
    #define IMAGE_PATH "/home/zhb/T113/app_sdk/build/app_client/res/image/"
#else
    #define FONT_PATH "/usr/res/font/"
    #define IMAGE_PATH "/usr/res/image/"
#endif

#endif