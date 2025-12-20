# rga_yolov11
orangepi 3b 8g 实现rga加速的零拷贝yolov11推理

# bug

1. Invalid RKNN model version 6

去官网下载最新的librknnrt.so替换/usr/lib下原来的库就行

2. 开启 rga 编译的时候可能会找不到 im2d.h 这些 rga 的头文件

克隆[官网源代码](https://github.com/airockchip/librga.git)，添加 include src 和 lib 即可