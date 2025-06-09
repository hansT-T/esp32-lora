# esp32+sx1276

## 项目概述

-时间同步&&安全识别

-main分支为时间同步，snick分支为安全识别

-本项目适用于heltec产品wifi lora v2，（或者使用esp32+sx1276自行实现绑定引脚，中断等或许也可以）

-原厂的库经过多次迭代后已于我们一直使用的大不相同了，因此一切基于本项目实现

-项目分为arduino，script，src三部分

-arduino文件中为烧录到节点的程序，主要控制节点的收发逻辑。

-script中为处理数据的脚本程序

-src为LoRa的库文件

## src使用说明

-使用前需安装hardware文件：WiFi_Kit_series.zip 链接: https://pan.baidu.com/s/1qLD-n0COJ-IBRDTPRVWlNA?pwd=ubsu 提取码: ubsu

-下载解压到arduino项目目录下的hardware中（arduino程序->文件->首选项->项目文件夹位置中，没有hardware自己新建一个即可）

-其中主要的库主要为D:\Arduino\hardware\WiFi_Kit_series\esp32\libraries\LoraWan102，即本项目src中的文件，建议在其它地方clone后，复制粘贴替换库

-主要功能为控制节点的发送/接收，并增加1.记录接收/发送中断时的时间，2.增加对跳频中断的处理，并且跳频发送中断时将此时的时间随payload一起发送

-main分支为主要实现，snick分支多增加了一个接收完成后，延迟delta后再发送的功能，搭配arduino目录中的code使用

-主要改动均集中在src/driver/sx1276.c中，改动可通过@Description查找

## 文件说明

-src/driver/sx1276.c  核心库文件，主要改动均在里面

-arduino/pingpong.ino  节点程序，负责一发一收，也可以改成一直发/一直收...

-script/linear.py  根据t1,t2提取skew和offset

-script/RxDoneTime.py 从节点的串口信息中提取出RxDoneTime，接收完成时间

-script/TxDoneTime.py 同上，发送完成

-script/TxHeaderTime.py 跳频发送中断时间

-script/RxHeaderTime.py 跳频接收中断事件    （仅节点和节点，且没有使用这个时间）

-script/gbm.py gbdt训练和识别程序

-script/knn1.py knn训练和识别程序