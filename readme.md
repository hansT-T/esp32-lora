# esp32+sx1276

## 项目概述

-本项目适用于heltec产品wifi lora v2，（或者使用esp32+sx1276自行实现绑定引脚，中断等或许也可以）

-原厂的库经过多次迭代后已于我们一直使用的大不相同了，因此一切基于本项目实现

-使用前需安装hardware文件：WiFi_Kit_series.zip 链接: https://pan.baidu.com/s/1qLD-n0COJ-IBRDTPRVWlNA?pwd=ubsu 提取码: ubsu

-下载解压到arduino项目目录下的hardware中（arduino程序->文件->首选项->项目文件夹位置中，没有hardware自己新建一个即可）

-其中主要的库主要为D:\Arduino\hardware\WiFi_Kit_series\esp32\libraries\LoraWan102，即本项目src中的文件，建议在其它地方clone后，复制粘贴替换库

-主要功能为控制节点的发送/接收，并增加1.记录接收/发送中断时的时间，2.增加对跳频中断的处理，并且跳频发送中断时将此时的时间随payload一起发送

-main分支为主要实现，snick分支多增加了一个接收完成后，延迟delta后再发送的功能，搭配arduino目录中的code使用

-主要改动均集中在src/driver/sx1276.c中，改动可通过@Description查找

-arduino文件中为烧录到节点的程序，主要控制节点的收发逻辑。