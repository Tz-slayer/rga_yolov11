#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <opencv2/opencv.hpp>
#include <time.h>

// V4L2相关头文件
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>

#include "image_utils.h"
#include "yolo11.h"
#include "postprocess.h"
#include "dma_alloc.h"

#ifdef USE_RGA
#include "im2d.h"
#include "RgaUtils.h"
#endif

/*-------------------------------------------
          时间测量辅助函数
-------------------------------------------*/
typedef struct
{
    struct timespec start;
    struct timespec end;
} my_timer_t;

void timer_start(my_timer_t *timer)
{
    clock_gettime(CLOCK_MONOTONIC, &timer->start);
}

double timer_end(my_timer_t *timer)
{
    clock_gettime(CLOCK_MONOTONIC, &timer->end);
    return (timer->end.tv_sec - timer->start.tv_sec) * 1000.0 +
           (timer->end.tv_nsec - timer->start.tv_nsec) / 1000000.0;
}

/*-------------------------------------------
          摄像头初始化和采集函数
-------------------------------------------*/
typedef struct
{
    int fd;
    unsigned char *mptr[4];
    unsigned int size[4];
    int dma_fd[4];  // DMA文件描述符
    int width;
    int height;
    int buffer_count;
    bool use_dmabuf;  // 是否使用DMABUF
} v4l2_camera_t;

int init_camera(v4l2_camera_t *camera, const char *device, int width, int height)
{
    int ret;

    if (!camera || !device)
    {
        printf("ERROR: Invalid camera parameters\n");
        return -1;
    }

    memset(camera, 0, sizeof(v4l2_camera_t));
    camera->fd = -1;
    for (int i = 0; i < 4; i++)
    {
        camera->dma_fd[i] = -1;
    }

    // 打开摄像头设备
    camera->fd = open(device, O_RDWR);
    if (camera->fd < 0)
    {
        perror("ERROR: 打开摄像头设备失败");
        return -1;
    }

    // 查询设备能力
    struct v4l2_capability cap;
    ret = ioctl(camera->fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0)
    {
        perror("ERROR: 查询设备能力失败");
        close(camera->fd);
        return -1;
    }
    printf("INFO: 摄像头设备: %s, 驱动: %s\n", cap.card, cap.driver);

    // 检查是否支持DMABUF
    bool dmabuf_supported = (cap.capabilities & V4L2_CAP_STREAMING) &&
                           (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE);

    // 设置摄像头采集格式
    struct v4l2_format vfmt;
    memset(&vfmt, 0, sizeof(vfmt));
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vfmt.fmt.pix.width = width;
    vfmt.fmt.pix.height = height;
    vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

    ret = ioctl(camera->fd, VIDIOC_S_FMT, &vfmt);
    if (ret < 0)
    {
        perror("ERROR: 设置格式失败");
        close(camera->fd);
        return -1;
    }

    printf("INFO: 实际格式: %dx%d, fourcc: %.4s\n",
           vfmt.fmt.pix.width, vfmt.fmt.pix.height,
           (char *)&vfmt.fmt.pix.pixelformat);

    size_t buf_size = width * height * 2;  // YUYV: 2 bytes per pixel

#ifdef USE_RGA
    // 尝试使用DMABUF模式
    struct v4l2_requestbuffers reqbuffer_dmabuf;
    memset(&reqbuffer_dmabuf, 0, sizeof(reqbuffer_dmabuf));
    reqbuffer_dmabuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuffer_dmabuf.count = 4;
    reqbuffer_dmabuf.memory = V4L2_MEMORY_DMABUF;

    ret = ioctl(camera->fd, VIDIOC_REQBUFS, &reqbuffer_dmabuf);
    if (ret == 0 && dmabuf_supported)
    {
        printf("INFO: 使用DMABUF模式\n");
        camera->use_dmabuf = true;

        // 为每个缓冲区分配DMA缓冲区
        for (int i = 0; i < 4; i++)
        {
            ret = dma_buf_alloc(DMA_HEAP_DMA32_UNCACHED_PATH, buf_size, 
                               &camera->dma_fd[i], (void **)&camera->mptr[i]);
            if (ret < 0)
            {
                printf("ERROR: DMA缓冲区分配失败 (index %d)\n", i);
                for (int j = 0; j < i; j++)
                {
                    dma_buf_free(buf_size, &camera->dma_fd[j], camera->mptr[j]);
                }
                close(camera->fd);
                return -1;
            }
            camera->size[i] = buf_size;

            // 将DMA缓冲区加入队列
            struct v4l2_buffer qbuf;
            memset(&qbuf, 0, sizeof(qbuf));
            qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            qbuf.memory = V4L2_MEMORY_DMABUF;
            qbuf.index = i;
            qbuf.m.fd = camera->dma_fd[i];

            ret = ioctl(camera->fd, VIDIOC_QBUF, &qbuf);
            if (ret < 0)
            {
                perror("ERROR: DMABUF QBUF失败");
                for (int j = 0; j <= i; j++)
                {
                    dma_buf_free(buf_size, &camera->dma_fd[j], camera->mptr[j]);
                }
                close(camera->fd);
                return -1;
            }
        }

        printf("INFO: 申请到 %d 个DMABUF缓冲区\n", 4);
    }
    else
#endif
    {
        // 回退到MMAP模式
        printf("INFO: 使用MMAP模式\n");
        camera->use_dmabuf = false;

        struct v4l2_requestbuffers reqbuffer;
        memset(&reqbuffer, 0, sizeof(reqbuffer));
        reqbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        reqbuffer.count = 4;
        reqbuffer.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(camera->fd, VIDIOC_REQBUFS, &reqbuffer);
        if (ret < 0)
        {
            perror("ERROR: 申请内核缓冲区失败");
            close(camera->fd);
            return -1;
        }

        printf("INFO: 申请到 %d 个缓冲区\n", reqbuffer.count);

        // 映射缓冲区到用户空间
        struct v4l2_buffer mapbuffer;
        for (int i = 0; i < 4; i++)
        {
            memset(&mapbuffer, 0, sizeof(mapbuffer));
            mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            mapbuffer.memory = V4L2_MEMORY_MMAP;
            mapbuffer.index = i;

            ret = ioctl(camera->fd, VIDIOC_QUERYBUF, &mapbuffer);
            if (ret < 0)
            {
                perror("ERROR: 查询缓冲区失败");
                for (int j = 0; j < i; j++)
                {
                    munmap(camera->mptr[j], camera->size[j]);
                }
                close(camera->fd);
                return -1;
            }

            camera->mptr[i] = (unsigned char *)mmap(NULL, mapbuffer.length,
                                                    PROT_READ | PROT_WRITE,
                                                    MAP_SHARED, camera->fd,
                                                    mapbuffer.m.offset);
            if (camera->mptr[i] == MAP_FAILED)
            {
                perror("ERROR: mmap失败");
                for (int j = 0; j < i; j++)
                {
                    munmap(camera->mptr[j], camera->size[j]);
                }
                close(camera->fd);
                return -1;
            }

            camera->size[i] = mapbuffer.length;

            // 将缓冲区放回队列
            ret = ioctl(camera->fd, VIDIOC_QBUF, &mapbuffer);
            if (ret < 0)
            {
                perror("ERROR: 放回缓冲区失败");
                for (int j = 0; j <= i; j++)
                {
                    munmap(camera->mptr[j], camera->size[j]);
                }
                close(camera->fd);
                return -1;
            }
        }
    }

    camera->width = width;
    camera->height = height;
    camera->buffer_count = 4;

    // 开始采集
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(camera->fd, VIDIOC_STREAMON, &type);
    if (ret < 0)
    {
        perror("ERROR: 开启视频流失败");
        if (camera->use_dmabuf)
        {
            for (int i = 0; i < 4; i++)
            {
                dma_buf_free(buf_size, &camera->dma_fd[i], camera->mptr[i]);
            }
        }
        else
        {
            for (int i = 0; i < 4; i++)
            {
                munmap(camera->mptr[i], camera->size[i]);
            }
        }
        close(camera->fd);
        return -1;
    }

    printf("INFO: 摄像头初始化成功: %dx%d, YUYV格式\n", width, height);
    return 0;
}

int capture_frame(v4l2_camera_t *camera, struct v4l2_buffer *buffer)
{
    if (!camera || !buffer)
    {
        return -1;
    }

    memset(buffer, 0, sizeof(*buffer));
    buffer->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer->memory = camera->use_dmabuf ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;

    int ret = ioctl(camera->fd, VIDIOC_DQBUF, buffer);
    if (ret < 0)
    {
        perror("ERROR: 读取帧数据失败");
        return -1;
    }
    return 0;
}

int release_frame(v4l2_camera_t *camera, struct v4l2_buffer *buffer)
{
    if (!camera || !buffer)
    {
        return -1;
    }

    if (camera->use_dmabuf)
    {
        buffer->m.fd = camera->dma_fd[buffer->index];
    }

    int ret = ioctl(camera->fd, VIDIOC_QBUF, buffer);
    if (ret < 0)
    {
        perror("ERROR: 放回缓冲区失败");
        return -1;
    }
    return 0;
}

void close_camera(v4l2_camera_t *camera)
{
    if (!camera || camera->fd < 0)
    {
        return;
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(camera->fd, VIDIOC_STREAMOFF, &type);

    if (camera->use_dmabuf)
    {
        for (int i = 0; i < camera->buffer_count; i++)
        {
            if (camera->dma_fd[i] >= 0)
            {
                dma_buf_free(camera->size[i], &camera->dma_fd[i], camera->mptr[i]);
            }
        }
    }
    else
    {
        for (int i = 0; i < camera->buffer_count; i++)
        {
            if (camera->mptr[i])
            {
                munmap(camera->mptr[i], camera->size[i]);
            }
        }
    }

    close(camera->fd);
    camera->fd = -1;
    printf("INFO: 摄像头已关闭\n");
}

/*-------------------------------------------
                  Main Function
-------------------------------------------*/
int main(int argc, char **argv)
{
    int ret = 0;
    rknn_app_context_t rknn_app_ctx;
    v4l2_camera_t camera;
    struct v4l2_buffer readbuffer;
    image_buffer_t src_image;
    my_timer_t timer;
    my_timer_t total_timer;  // 新增总定时器

    // 初始化所有结构体
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_ctx));
    memset(&camera, 0, sizeof(camera));
    memset(&src_image, 0, sizeof(src_image));
    camera.fd = -1;

    bool camera_initialized = false;
    bool model_initialized = false;
    bool image_allocated = false;

    const char *model_path = "../model/yolo11n.rknn";
    const char *camera_device = "/dev/video0";
    const int cam_width = 640;
    const int cam_height = 480;

    printf("\n========== 开始测试 ==========\n\n");

    // 开始总定时器
    timer_start(&total_timer);

    // 1. 初始化后处理
    printf("1. 初始化后处理模块...\n");
    init_post_process();

    // 2. 初始化YOLO11模型
    printf("2. 加载YOLO11模型: %s\n", model_path);
    timer_start(&timer);
    ret = init_yolo11_model(model_path, &rknn_app_ctx);
    if (ret != 0)
    {
        printf("ERROR: 模型加载失败! ret=%d\n", ret);
        goto cleanup;
    }
    model_initialized = true;
    printf("   模型加载成功，耗时: %.2f ms\n\n", timer_end(&timer));

    // 3. 初始化摄像头
    printf("3. 初始化摄像头: %s\n", camera_device);
    ret = init_camera(&camera, camera_device, cam_width, cam_height);
    if (ret != 0)
    {
        printf("ERROR: 摄像头初始化失败\n");
        goto cleanup;
    }
    camera_initialized = true;
    printf("\n");

    // 4. 采集一帧数据
    printf("4. 采集图像帧...\n");
    ret = capture_frame(&camera, &readbuffer);
    if (ret != 0)
    {
        printf("ERROR: 采集帧失败\n");
        goto cleanup;
    }
    printf("   成功采集帧 index=%d, bytesused=%d, length=%d\n\n",
           readbuffer.index, readbuffer.bytesused, readbuffer.length);

    // 5. 分配目标缓冲区
    printf("5. 分配RGB888缓冲区...\n");
    src_image.width = cam_width;
    src_image.height = cam_height;
    src_image.format = IMAGE_FORMAT_RGB888;
    src_image.size = cam_width * cam_height * 3;

#ifdef ZERO_COPY
    printf("   使用DMA缓冲区 (size=%d bytes)\n", src_image.size);
    ret = dma_buf_alloc(DMA_HEAP_DMA32_UNCACHED_PATH, src_image.size,
                        &rknn_app_ctx.img_dma_buf.dma_buf_fd,
                        (void **)&(rknn_app_ctx.img_dma_buf.dma_buf_virt_addr));

    if (ret != 0)
    {
        printf("ERROR: DMA缓冲区分配失败\n");
        goto cleanup;
    }
    src_image.virt_addr = (unsigned char *)rknn_app_ctx.img_dma_buf.dma_buf_virt_addr;
    src_image.fd = rknn_app_ctx.img_dma_buf.dma_buf_fd;
    rknn_app_ctx.img_dma_buf.size = src_image.size;
    image_allocated = true;
    printf("   DMA缓冲区分配成功 (fd=%d)\n\n", src_image.fd);
#else
    printf("   使用普通内存 (size=%d bytes)\n", src_image.size);
    src_image.virt_addr = (unsigned char *)malloc(src_image.size);
    if (src_image.virt_addr == NULL)
    {
        printf("ERROR: 内存分配失败\n");
        goto cleanup;
    }
    image_allocated = true;
    printf("   内存分配成功\n\n");
#endif

    // 6. 格式转换: YUYV -> RGB888
    printf("6. 格式转换 YUYV -> RGB888...\n");
    timer_start(&timer);

#ifdef USE_RGA
    { // 添加作用域
        printf("   使用RGA硬件加速\n");

        rga_buffer_t src_img, dst_img;
        rga_buffer_handle_t src_handle = 0, dst_handle = 0;
        int src_format = RK_FORMAT_YUYV_422;
        int dst_format = RK_FORMAT_RGB_888;

        size_t src_buf_size = cam_width * cam_height * get_bpp_from_format(src_format);
        size_t dst_buf_size = cam_width * cam_height * get_bpp_from_format(dst_format);

        // 如果使用DMABUF模式，直接使用摄像头的DMA缓冲区
        if (camera.use_dmabuf)
        {
            printf("   直接使用摄像头DMABUF (零拷贝源)\n");
            src_handle = importbuffer_fd(camera.dma_fd[readbuffer.index], src_buf_size);
        }
        else
        {
            // MMAP模式需要分配临时DMA缓冲区并拷贝
            int src_dma_fd;
            char *src_dma_buf;
            ret = dma_buf_alloc(DMA_HEAP_DMA32_UNCACHED_PATH, src_buf_size, &src_dma_fd, (void **)&src_dma_buf);
            if (ret < 0)
            {
                printf("ERROR: 分配源DMA缓冲区失败\n");
                goto cleanup;
            }

            memcpy(src_dma_buf, camera.mptr[readbuffer.index], src_buf_size);
            src_handle = importbuffer_fd(src_dma_fd, src_buf_size);
            
            if (src_handle == 0)
            {
                printf("ERROR: RGA importbuffer_fd失败 (src)\n");
                dma_buf_free(src_buf_size, &src_dma_fd, src_dma_buf);
                goto cleanup;
            }
        }

        if (src_handle == 0)
        {
            printf("ERROR: RGA importbuffer_fd失败 (src)\n");
            goto cleanup;
        }

#ifdef ZERO_COPY
        // 使用src_image的DMA缓冲区作为目标（零拷贝）
        dst_handle = importbuffer_fd(rknn_app_ctx.img_dma_buf.dma_buf_fd, dst_buf_size);
        if (dst_handle == 0)
        {
            printf("ERROR: RGA importbuffer_fd失败 (dst)\n");
            releasebuffer_handle(src_handle);
            goto cleanup;
        }
#else
        // 非ZERO_COPY下，仍需分配目标DMA缓冲区
        int dst_dma_fd;
        char *dst_dma_buf;
        ret = dma_buf_alloc(DMA_HEAP_DMA32_UNCACHED_PATH, dst_buf_size, &dst_dma_fd, (void **)&dst_dma_buf);
        if (ret < 0)
        {
            printf("ERROR: 分配目标DMA缓冲区失败\n");
            releasebuffer_handle(src_handle);
            goto cleanup;
        }
        dst_handle = importbuffer_fd(dst_dma_fd, dst_buf_size);
        if (dst_handle == 0)
        {
            printf("ERROR: RGA importbuffer_fd失败 (dst)\n");
            releasebuffer_handle(src_handle);
            dma_buf_free(dst_buf_size, &dst_dma_fd, dst_dma_buf);
            goto cleanup;
        }
#endif

        // 封装为RGA图像结构
        src_img = wrapbuffer_handle(src_handle, cam_width, cam_height, src_format);
        dst_img = wrapbuffer_handle(dst_handle, cam_width, cam_height, dst_format);

        // 检查参数
        IM_STATUS status = imcheck(src_img, dst_img, {}, {});
        if (IM_STATUS_NOERROR != status)
        {
            printf("ERROR: RGA imcheck失败! %s\n", imStrError(status));
            releasebuffer_handle(src_handle);
            releasebuffer_handle(dst_handle);
            goto cleanup;
        }

        // 执行颜色空间转换
        status = imcvtcolor(src_img, dst_img, src_format, dst_format);
        if (IM_STATUS_SUCCESS != status)
        {
            printf("ERROR: RGA imcvtcolor失败! %s\n", imStrError(status));
            releasebuffer_handle(src_handle);
            releasebuffer_handle(dst_handle);
            goto cleanup;
        }

#ifndef ZERO_COPY
        // 非ZERO_COPY下，将转换后的数据复制到src_image.virt_addr
        memcpy(src_image.virt_addr, dst_dma_buf, dst_buf_size);
        dma_buf_free(dst_buf_size, &dst_dma_fd, dst_dma_buf);
#endif

        // 释放RGA句柄
        releasebuffer_handle(src_handle);
        releasebuffer_handle(dst_handle);

        printf("   RGA转换完成，耗时: %.2f ms\n\n", timer_end(&timer));
    } // 结束作用域
#else
    // 如果不使用 RGA 使用 opencv 进行转换
    { // 添加作用域以避免goto跨越初始化
        printf("   使用OpenCV转换\n");
        cv::Mat yuyv_mat(cam_height, cam_width, CV_8UC2, camera.mptr[readbuffer.index]);
        cv::Mat rgb_mat;
        cv::cvtColor(yuyv_mat, rgb_mat, cv::COLOR_YUV2RGB_YUYV);
        memcpy(src_image.virt_addr, rgb_mat.data, src_image.size);
        printf("   OpenCV转换完成，耗时: %.2f ms\n\n", timer_end(&timer));
    } // 结束作用域
#endif

#ifdef ZERO_COPY
    // 同步到设备
    dma_sync_cpu_to_device(rknn_app_ctx.img_dma_buf.dma_buf_fd);
#endif

    // 7. 执行YOLO推理
    printf("7. 执行YOLO推理...\n");
    timer_start(&timer);

    object_detect_result_list od_results;
    ret = inference_yolo11_model(&rknn_app_ctx, &src_image, &od_results);
    if (ret != 0)
    {
        printf("ERROR: 推理失败! ret=%d\n", ret);
        goto cleanup;
    }

    printf("   推理完成，耗时: %.2f ms\n\n", timer_end(&timer));

    // 8. 处理检测结果
    printf("8. 检测结果:\n");
    printf("   检测到 %d 个目标\n", od_results.count);

    char text[256];
    for (int i = 0; i < od_results.count; i++)
    {
        object_detect_result *det_result = &(od_results.results[i]);
        printf("   [%d] %s @ (%d,%d)-(%d,%d) 置信度: %.1f%%\n",
               i + 1,
               coco_cls_to_name(det_result->cls_id),
               det_result->box.left, det_result->box.top,
               det_result->box.right, det_result->box.bottom,
               det_result->prop * 100);

        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;

        // 绘制边界框
        draw_rectangle(&src_image, x1, y1, x2 - x1, y2 - y1, COLOR_BLUE, 3);

        // 绘制标签
        snprintf(text, sizeof(text), "%s %.1f%%",
                 coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
        draw_text(&src_image, text, x1, y1 - 20, COLOR_RED, 10);
    }
    printf("\n");

    // 9. 保存结果图像
    printf("9. 保存结果图像...\n");
    ret = write_image("out.png", &src_image);
    if (ret == 0)
    {
        printf("   结果已保存到 out.png\n");
    }
    else
    {
        printf("WARNING: 保存图像失败\n");
    }

    // 放回缓冲区
    release_frame(&camera, &readbuffer);

    printf("\n========== 测试完成 ==========\n\n");

cleanup:
    // 结束总定时器并打印总耗时
    printf("总耗时: %.2f ms\n", timer_end(&total_timer));

    // 清理资源
    printf("清理资源...\n");

    if (camera_initialized)
    {
        close_camera(&camera);
    }

    deinit_post_process();

    if (model_initialized)
    {
        ret = release_yolo11_model(&rknn_app_ctx);
        if (ret != 0)
        {
            printf("WARNING: 释放模型失败 ret=%d\n", ret);
        }
    }

    if (image_allocated && src_image.virt_addr)
    {
#ifdef ZERO_COPY
        dma_buf_free(rknn_app_ctx.img_dma_buf.size,
                     &rknn_app_ctx.img_dma_buf.dma_buf_fd,
                     rknn_app_ctx.img_dma_buf.dma_buf_virt_addr);
#else
        free(src_image.virt_addr);
#endif
        src_image.virt_addr = NULL;
    }

    printf("清理完成\n");
    return ret;
}