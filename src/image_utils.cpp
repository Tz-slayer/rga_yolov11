#include "image_utils.h"
#include <cstddef>
#include "im2d.h"
#include <fstream>

#include <sstream>
#include <vector>
#include <string>
#include <iostream>
#include <opencv2/opencv.hpp>

int draw_text(image_buffer_t* src_image, const char* text, int x, int y, int color, int font_size)
{
    if (!src_image || !src_image->virt_addr || !text) {
        printf("Invalid parameters\n");
        return -1;
    }

    // 根据图像格式设置 OpenCV 类型
    int cv_type = CV_8UC1;  // 默认灰度
    int channels = 1;
    switch (src_image->format) {
        case IMAGE_FORMAT_GRAY8:
            cv_type = CV_8UC1;
            channels = 1;
            break;
        case IMAGE_FORMAT_RGB888:
            cv_type = CV_8UC3;
            channels = 3;
            break;
        case IMAGE_FORMAT_RGBA8888:
            cv_type = CV_8UC4;
            channels = 4;
            break;
        default:
            printf("Unsupported format for drawing text\n");
            return -1;
    }

    // 创建 OpenCV Mat（共享内存）
    cv::Mat img(src_image->height, src_image->width, cv_type, src_image->virt_addr);

    // 设置颜色（根据通道数）
    cv::Scalar cv_color;
    if (channels == 1) {
        cv_color = cv::Scalar(color);  // 灰度
    } else if (channels == 3) {
        cv_color = cv::Scalar(color, color, color);  // RGB
    } else if (channels == 4) {
        cv_color = cv::Scalar(color, color, color, 255);  // RGBA
    }

    // 绘制文本
    cv::putText(img, text, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, 
                font_size / 10.0, cv_color, 2, cv::LINE_AA);

    printf("Drew text '%s' at (%d,%d) with color %d, font size %d\n", 
           text, x, y, color, font_size);
    return 0;
}

int draw_rectangle_rga(image_buffer_t* src_image, int x, int y, int width, int height, int color, int thickness)
{
    if (!src_image) {
        printf("Invalid image structure\n");
        return -1;
    }

    // 1. 格式转换：将自定义格式映射到 RGA 格式
    int rga_format = RK_FORMAT_RGB_888;
    switch (src_image->format) {
        case IMAGE_FORMAT_GRAY8:
            rga_format = RK_FORMAT_YCbCr_400; 
            break;
        case IMAGE_FORMAT_RGB888:
            rga_format = RK_FORMAT_RGB_888;
            break;
        case IMAGE_FORMAT_RGBA8888:
            rga_format = RK_FORMAT_RGBA_8888;
            break;
        // ... 添加其他格式支持，如 NV12 等
        default:
            printf("Unsupported format for RGA: %d\n", src_image->format);
            return -1;
    }

    // 2. 包装缓冲区 (RGA Buffer Wrapping)
    rga_buffer_t target_buf;
    rga_buffer_handle_t handle = 0; // 用于管理 fd 的句柄

    // [关键优化]：优先检查是否存在 dma_fd (官方Demo推荐方式)
    if (src_image->fd > 0) {
        // 如果有 size 信息最好，没有则估算
        size_t buf_size = (src_image->size > 0) ? src_image->size : (src_image->width * src_image->height * 4);
        
        // 导入 fd，获取 handle
        handle = importbuffer_fd(src_image->fd, buf_size);
        if (handle == 0) {
            printf("Failed to import dma_fd\n");
            return -1;
        }
        
        // 通过 handle 包装 buffer
        target_buf = wrapbuffer_handle(handle, src_image->width, src_image->height, rga_format);
    } 
    // [兼容模式]：如果只有虚拟地址 (virt_addr)
    else if (src_image->virt_addr) {
        // 直接通过虚拟地址包装，性能稍低但通用性强
        target_buf = wrapbuffer_virtualaddr(src_image->virt_addr, src_image->width, src_image->height, rga_format);
    } else {
        printf("No valid fd or virt_addr found in image buffer\n");
        return -1;
    }

    // 3. 设置绘制区域 (im_rect)
    im_rect rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;

    // 4. 执行绘制 (imrectangle)
    // 官方 demo 使用的就是这个 API，它会自动处理空心矩形的绘制
    // color 格式通常为 0xAABBGGRR (Little Endian)
    IM_STATUS status = imrectangle(target_buf, rect, color, thickness);

    // 5. 资源释放
    // [非常重要] 如果使用了 importbuffer_fd，必须释放 handle，否则会导致 RGA 驱动资源耗尽
    if (handle > 0) {
        releasebuffer_handle(handle);
    }

    if (status != IM_STATUS_SUCCESS) {
        printf("RGA draw rectangle failed: %s\n", imStrError(status));
        return -1;
    }

    return 0;
}

int draw_rectangle_opencv(image_buffer_t* src_image, int x, int y, int width, int height, int color, int thickness)
{
    if (!src_image || !src_image->virt_addr) {
        printf("Invalid image buffer\n");
        return -1;
    }

    // 根据图像格式设置 OpenCV 类型
    int cv_type = CV_8UC1;  // 默认灰度
    int channels = 1;
    switch (src_image->format) {
        case IMAGE_FORMAT_GRAY8:
            cv_type = CV_8UC1;
            channels = 1;
            break;
        case IMAGE_FORMAT_RGB888:
            cv_type = CV_8UC3;
            channels = 3;
            break;
        case IMAGE_FORMAT_RGBA8888:
            cv_type = CV_8UC4;
            channels = 4;
            break;
        default:
            printf("Unsupported format for drawing\n");
            return -1;
    }

    // 创建 OpenCV Mat（共享内存）
    cv::Mat img(src_image->height, src_image->width, cv_type, src_image->virt_addr);

    // 设置颜色（根据通道数）
    cv::Scalar cv_color;
    if (channels == 1) {
        cv_color = cv::Scalar(color);  // 灰度
    } else if (channels == 3) {
        cv_color = cv::Scalar(color, color, color);  // RGB
    } else if (channels == 4) {
        cv_color = cv::Scalar(color, color, color, 255);  // RGBA
    }

    // 绘制矩形
    cv::Rect rect(x, y, width, height);
    cv::rectangle(img, rect, cv_color, thickness);

    printf("Drew rectangle at (%d,%d) size %dx%d with color %d, thickness %d\n", 
           x, y, width, height, color, thickness);
    return 0;
}

int draw_rectangle(image_buffer_t* src_image, int x, int y, int width, int height, int color, int thickness)
{
    int ret;
#ifdef USE_RGA
    if(src_image->width % 16 == 0) {
        ret = draw_rectangle_rga(src_image, x, y, width, height, color, thickness);
        if (ret != 0) {
            printf("draw rectangle use rga failed\n");
            return -1;
        }
    } else {
        printf("using rga now, and src width is not 4/16-aligned\n");
        return -1;  
    }
#else
    ret = draw_rectangle_opencv(src_image, x, y, width, height, color, thickness);
#endif
    return ret;
}

int write_image(const char* path, const image_buffer_t* image)
{
    if (!image || !image->virt_addr) 
    {
        printf("Invalid image buffer\n");
        return -1;
    }
    // set image format
    int cv_type = CV_8UC1;  // default gray
    int channels = 1;
    switch (image->format)
    {
        case IMAGE_FORMAT_GRAY8:
            cv_type = CV_8UC1;
            channels = 1;
            break;
        case IMAGE_FORMAT_RGB888:
            cv_type = CV_8UC3;
            channels = 3;
            break;
        case IMAGE_FORMAT_RGBA8888:
            cv_type = CV_8UC4;
            channels = 4;
            break;
        default:
            printf("Unsupported format for writing\n");
            return -1;
    }

    // create opencv mat
    cv::Mat img(image->height, image->width, cv_type, image->virt_addr);
    // save image
    if (!cv::imwrite(path, img)) {
        printf("failed to write image to %s\n", path);
        return -1;
    }
    printf("successfully wrote image to %s (%dx%d, format=%d)\n", 
           path, image->width, image->height, image->format);
    return 0;
}

int get_image_size(image_buffer_t *image)
{
    if (image == NULL)
    {
        return 0;
    }
    switch (image->format)
    {
    case IMAGE_FORMAT_GRAY8:
        return image->width * image->height;
    case IMAGE_FORMAT_RGB888:
        return image->width * image->height * 3;
    case IMAGE_FORMAT_RGBA8888:
        return image->width * image->height * 4;
    case IMAGE_FORMAT_YUV420SP_NV12:
    case IMAGE_FORMAT_YUV420SP_NV21:
        return image->width * image->height * 3 / 2;
    default:
        break;
    }
}

// read image
int read_image(const char *image_path, image_buffer_t *image)
{
    // read image use opencv
    cv::Mat img = cv::imread(image_path, cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        printf("failed to read image with opencv: %s\n", image_path);
        return -1;
    }

    // set image width and height
    image->width = img.cols;
    image->height = img.rows;

    // set pixel format
    switch (img.channels()) {
        case 1:
            image->format = IMAGE_FORMAT_GRAY8;
            break;
        case 3:
            image->format = IMAGE_FORMAT_RGB888;
            break;
        case 4:
            image->format = IMAGE_FORMAT_RGBA8888;
            break;
        default:
            printf("unsupported image format with %d channels\n", img.channels());
            return -1;
    }

    // calculate image size
    image->size = get_image_size(image);
    if (image->size == 0) {
        printf("failed to calculate image size\n");
        return -1;
    }

    // malloc mem
    image->virt_addr = (uint8_t *)malloc(image->size);
    if (image->virt_addr == NULL) {
        printf("failed to allocate memory for image data\n");
        return -1;
    }

    // copy image data
    if (img.channels() == 3) {
        cv::Mat rgb_img;
        cv::cvtColor(img, rgb_img, cv::COLOR_BGR2RGB);
        memcpy(image->virt_addr, rgb_img.data, image->size);
    } else {
        memcpy(image->virt_addr, img.data, image->size);
    }

    printf("successfully read image: %s (%dx%d, format=%d)\n", 
             image_path, image->width, image->height, image->format);
    return 0;
}


static int get_rga_fmt(image_format_t fmt) {
    switch (fmt)
    {
    case IMAGE_FORMAT_RGB888:
        return RK_FORMAT_RGB_888;
    case IMAGE_FORMAT_RGBA8888:
        return RK_FORMAT_RGBA_8888;
    case IMAGE_FORMAT_YUV420SP_NV12:
        return RK_FORMAT_YCbCr_420_SP;
    case IMAGE_FORMAT_YUV420SP_NV21:
        return RK_FORMAT_YCrCb_420_SP;
    default:
        return -1;
    }
}

static int convert_image_rga(image_buffer_t* src_img, image_buffer_t* dst_img, image_rect_t* src_box, image_rect_t* dst_box, char color)
{
    int ret = 0;

    int srcWidth = src_img->width;
    int srcHeight = src_img->height;
    void *src = src_img->virt_addr;
    int src_fd = src_img->fd;
    void *src_phy = NULL;
    int srcFmt = get_rga_fmt(src_img->format);

    int dstWidth = dst_img->width;
    int dstHeight = dst_img->height;
    void *dst = dst_img->virt_addr;
    int dst_fd = dst_img->fd;
    void *dst_phy = NULL;
    int dstFmt = get_rga_fmt(dst_img->format);

    int rotate = 0;

    int use_handle = 0;

    // printf("src width=%d height=%d fmt=0x%x virAddr=0x%p fd=%d\n",
    //     srcWidth, srcHeight, srcFmt, src, src_fd);
    // printf("dst width=%d height=%d fmt=0x%x virAddr=0x%p fd=%d\n",
    //     dstWidth, dstHeight, dstFmt, dst, dst_fd);
    // printf("rotate=%d\n", rotate);

    int usage = 0;
    IM_STATUS ret_rga = IM_STATUS_NOERROR;

    // set rga usage
    usage |= rotate;

    // set rga rect
    im_rect srect;
    im_rect drect;
    im_rect prect;
    memset(&prect, 0, sizeof(im_rect));

    if (src_box != NULL) {
        srect.x = src_box->left;
        srect.y = src_box->top;
        srect.width = src_box->right - src_box->left + 1;
        srect.height = src_box->bottom - src_box->top + 1;
    } else {
        srect.x = 0;
        srect.y = 0;
        srect.width = srcWidth;
        srect.height = srcHeight;
    }

    if (dst_box != NULL) {
        drect.x = dst_box->left;
        drect.y = dst_box->top;
        drect.width = dst_box->right - dst_box->left + 1;
        drect.height = dst_box->bottom - dst_box->top + 1;
    } else {
        drect.x = 0;
        drect.y = 0;
        drect.width = dstWidth;
        drect.height = dstHeight;
    }

    // set rga buffer
    rga_buffer_t rga_buf_src;
    rga_buffer_t rga_buf_dst;
    rga_buffer_t pat;
    rga_buffer_handle_t rga_handle_src = 0;
    rga_buffer_handle_t rga_handle_dst = 0;
    memset(&pat, 0, sizeof(rga_buffer_t));

    im_handle_param_t in_param;
    in_param.width = srcWidth;
    in_param.height = srcHeight;
    in_param.format = srcFmt;

    im_handle_param_t dst_param;
    dst_param.width = dstWidth;
    dst_param.height = dstHeight;
    dst_param.format = dstFmt;

    if (use_handle) {
        if (src_phy != NULL) {
            rga_handle_src = importbuffer_physicaladdr((uint64_t)src_phy, &in_param);
        } else if (src_fd > 0) {
            rga_handle_src = importbuffer_fd(src_fd, &in_param);
        } else {
            rga_handle_src = importbuffer_virtualaddr(src, &in_param);
        }
        if (rga_handle_src <= 0) {
            printf("src handle error %d\n", rga_handle_src);
            ret = -1;
            goto err;
        }
        rga_buf_src = wrapbuffer_handle(rga_handle_src, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
    } else {
        if (src_phy != NULL) {
            rga_buf_src = wrapbuffer_physicaladdr(src_phy, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
        } else if (src_fd > 0) {
            rga_buf_src = wrapbuffer_fd(src_fd, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
        } else {
            rga_buf_src = wrapbuffer_virtualaddr(src, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
        }
    }

    if (use_handle) {
        if (dst_phy != NULL) {
            rga_handle_dst = importbuffer_physicaladdr((uint64_t)dst_phy, &dst_param);
        } else if (dst_fd > 0) {
            rga_handle_dst = importbuffer_fd(dst_fd, &dst_param);
        } else {
            rga_handle_dst = importbuffer_virtualaddr(dst, &dst_param);
        }
        if (rga_handle_dst <= 0) {
            printf("dst handle error %d\n", rga_handle_dst);
            ret = -1;
            goto err;
        }
        rga_buf_dst = wrapbuffer_handle(rga_handle_dst, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
    } else {
        if (dst_phy != NULL) {
            rga_buf_dst = wrapbuffer_physicaladdr(dst_phy, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
        } else if (dst_fd > 0) {
            rga_buf_dst = wrapbuffer_fd(dst_fd, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
        } else {
            rga_buf_dst = wrapbuffer_virtualaddr(dst, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
        }
    }

    if (drect.width != dstWidth || drect.height != dstHeight) {
        im_rect dst_whole_rect = {0, 0, dstWidth, dstHeight};
        int imcolor;
        char* p_imcolor = (char *)&imcolor;
        p_imcolor[0] = color;
        p_imcolor[1] = color;
        p_imcolor[2] = color;
        p_imcolor[3] = color;
        printf("fill dst image (x y w h)=(%d %d %d %d) with color=0x%x\n",
            dst_whole_rect.x, dst_whole_rect.y, dst_whole_rect.width, dst_whole_rect.height, imcolor);
        ret_rga = imfill(rga_buf_dst, dst_whole_rect, imcolor);
        if (ret_rga <= 0) {
            if (dst != NULL) {
                size_t dst_size = get_image_size(dst_img);
                memset(dst, color, dst_size);
            } else {
                printf("Warning: Can not fill color on target image\n");
            }
        }
    }

    // rga process
    ret_rga = improcess(rga_buf_src, rga_buf_dst, pat, srect, drect, prect, usage);
    if (ret_rga <= 0) {
        printf("Error on improcess STATUS=%d\n", ret_rga);
        printf("RGA error message: %s\n", imStrError((IM_STATUS)ret_rga));
        ret = -1;
    }

err:
    if (rga_handle_src > 0) {
        releasebuffer_handle(rga_handle_src);
    }

    if (rga_handle_dst > 0) {
        releasebuffer_handle(rga_handle_dst);
    }

    // printf("finish\n");
    return ret;
}


int convert_image_opencv(image_buffer_t* src_img, image_buffer_t* dst_img, image_rect_t* src_box, image_rect_t* dst_box, char color)
{
    if (!src_img || !dst_img || src_img->format != dst_img->format) {
        printf("Invalid parameters or format mismatch\n");
        return -1;
    }

    // ✅ 1. 创建 OpenCV Mat 对象
    cv::Mat src_mat, dst_mat;
    int channels = 0;
    int cv_type = CV_8UC1;  // 默认灰度

    switch (src_img->format) {
        case IMAGE_FORMAT_GRAY8:
            channels = 1;
            cv_type = CV_8UC1;
            break;
        case IMAGE_FORMAT_RGB888:
            channels = 3;
            cv_type = CV_8UC3;
            break;
        case IMAGE_FORMAT_RGBA8888:
            channels = 4;
            cv_type = CV_8UC4;
            break;
        default:
            printf("Unsupported format\n");
            return -1;
    }

    // 创建源图像 Mat
    src_mat = cv::Mat(src_img->height, src_img->width, cv_type, src_img->virt_addr);

    // 创建目标图像 Mat（如果未分配内存，则分配）
    if (dst_img->virt_addr == NULL) {
        dst_img->size = get_image_size(dst_img);
        dst_img->virt_addr = (uint8_t*)malloc(dst_img->size);
        if (!dst_img->virt_addr) {
            printf("Failed to allocate memory for dst image\n");
            return -1;
        }
    }
    dst_mat = cv::Mat(dst_img->height, dst_img->width, cv_type, dst_img->virt_addr);

    // ✅ 2. 处理源裁剪区域
    cv::Rect src_roi(0, 0, src_img->width, src_img->height);
    if (src_box) {
        src_roi = cv::Rect(src_box->left, src_box->top, 
                          src_box->right - src_box->left + 1, 
                          src_box->bottom - src_box->top + 1);
        // 确保 ROI 在图像范围内
        src_roi &= cv::Rect(0, 0, src_img->width, src_img->height);
    }

    // ✅ 3. 处理目标放置区域
    cv::Rect dst_roi(0, 0, dst_img->width, dst_img->height);
    if (dst_box) {
        dst_roi = cv::Rect(dst_box->left, dst_box->top, 
                          dst_box->right - dst_box->left + 1, 
                          dst_box->bottom - dst_box->top + 1);
        // 确保 ROI 在图像范围内
        dst_roi &= cv::Rect(0, 0, dst_img->width, dst_img->height);
    }

    // ✅ 4. 填充背景色
    cv::Scalar bg_color(color, color, color, color);  // RGBA
    dst_mat.setTo(bg_color);

    // ✅ 5. 裁剪源图像
    cv::Mat cropped_src = src_mat(src_roi);

    // ✅ 6. 缩放到目标 ROI 大小
    cv::Mat resized_src;
    cv::resize(cropped_src, resized_src, dst_roi.size(), 0, 0, cv::INTER_LINEAR);

    // ✅ 7. 复制到目标位置
    cv::Mat dst_roi_mat = dst_mat(dst_roi);
    resized_src.copyTo(dst_roi_mat);

    printf("convert_image using OpenCV: src(%dx%d) -> dst(%dx%d), roi_src(%d,%d,%d,%d) roi_dst(%d,%d,%d,%d)\n",
           src_img->width, src_img->height, dst_img->width, dst_img->height,
           src_roi.x, src_roi.y, src_roi.width, src_roi.height,
           dst_roi.x, dst_roi.y, dst_roi.width, dst_roi.height);

    return 0;
}


int convert_image(image_buffer_t* src_img, image_buffer_t* dst_img, image_rect_t* src_box, image_rect_t* dst_box, char color)
{
    int ret;
#ifdef USE_RGA
    if(src_img->width % 16 == 0 && dst_img->width % 16 == 0) {
        ret = convert_image_rga(src_img, dst_img, src_box, dst_box, color);
        if (ret != 0) {
            printf("convert image use rga failed\n");
            return -1;
        }
    } else {
        printf("using rga now, and src width is not 4/16-aligned\n");
        return -1;
    }
#else
    ret = convert_image_opencv(src_img, dst_img, src_box, dst_box, color);
#endif
    return ret;
}

// int convert_image(image_buffer_t* src_img, image_buffer_t* dst_img, image_rect_t* src_box, image_rect_t* dst_box, char color)
// {
//     if (!src_img || !dst_img || src_img->format != dst_img->format) {
//         printf("Invalid parameters or format mismatch\n");
//         return -1;
//     }

//     // ✅ 1. 创建 OpenCV Mat 对象
//     cv::Mat src_mat, dst_mat;
//     int channels = 0;
//     int cv_type = CV_8UC1;  // 默认灰度

//     switch (src_img->format) {
//         case IMAGE_FORMAT_GRAY8:
//             channels = 1;
//             cv_type = CV_8UC1;
//             break;
//         case IMAGE_FORMAT_RGB888:
//             channels = 3;
//             cv_type = CV_8UC3;
//             break;
//         case IMAGE_FORMAT_RGBA8888:
//             channels = 4;
//             cv_type = CV_8UC4;
//             break;
//         default:
//             printf("Unsupported format\n");
//             return -1;
//     }

//     // 创建源图像 Mat
//     src_mat = cv::Mat(src_img->height, src_img->width, cv_type, src_img->virt_addr);

//     // 创建目标图像 Mat（如果未分配内存，则分配）
//     if (dst_img->virt_addr == NULL) {
//         dst_img->size = get_image_size(dst_img);
//         dst_img->virt_addr = (uint8_t*)malloc(dst_img->size);
//         if (!dst_img->virt_addr) {
//             printf("Failed to allocate memory for dst image\n");
//             return -1;
//         }
//     }
//     dst_mat = cv::Mat(dst_img->height, dst_img->width, cv_type, dst_img->virt_addr);

//     // ✅ 2. 处理源裁剪区域
//     cv::Rect src_roi(0, 0, src_img->width, src_img->height);
//     if (src_box) {
//         src_roi = cv::Rect(src_box->left, src_box->top, 
//                           src_box->right - src_box->left + 1, 
//                           src_box->bottom - src_box->top + 1);
//         // 确保 ROI 在图像范围内
//         src_roi &= cv::Rect(0, 0, src_img->width, src_img->height);
//     }

//     // ✅ 3. 处理目标放置区域
//     cv::Rect dst_roi(0, 0, dst_img->width, dst_img->height);
//     if (dst_box) {
//         dst_roi = cv::Rect(dst_box->left, dst_box->top, 
//                           dst_box->right - dst_box->left + 1, 
//                           dst_box->bottom - dst_box->top + 1);
//         // 确保 ROI 在图像范围内
//         dst_roi &= cv::Rect(0, 0, dst_img->width, dst_img->height);
//     }

//     // ✅ 4. 填充背景色
//     cv::Scalar bg_color(color, color, color, color);  // RGBA
//     dst_mat.setTo(bg_color);

//     // ✅ 5. 裁剪源图像
//     cv::Mat cropped_src = src_mat(src_roi);

//     // ✅ 6. 缩放到目标 ROI 大小
//     cv::Mat resized_src;
//     cv::resize(cropped_src, resized_src, dst_roi.size(), 0, 0, cv::INTER_LINEAR);

//     // ✅ 7. 复制到目标位置
//     cv::Mat dst_roi_mat = dst_mat(dst_roi);
//     resized_src.copyTo(dst_roi_mat);

//     printf("convert_image using OpenCV: src(%dx%d) -> dst(%dx%d), roi_src(%d,%d,%d,%d) roi_dst(%d,%d,%d,%d)\n",
//            src_img->width, src_img->height, dst_img->width, dst_img->height,
//            src_roi.x, src_roi.y, src_roi.width, src_roi.height,
//            dst_roi.x, dst_roi.y, dst_roi.width, dst_roi.height);

//     return 0;
// }

int convert_image_with_letterbox(image_buffer_t* src_image, image_buffer_t* dst_image, letterbox_t* letterbox, char color)
{
    int ret = 0;
    int allow_slight_change = 1;
    int src_w = src_image->width;
    int src_h = src_image->height;
    int dst_w = dst_image->width;
    int dst_h = dst_image->height;
    int resize_w = dst_w;
    int resize_h = dst_h;

    int padding_w = 0;
    int padding_h = 0;

    int _left_offset = 0;
    int _top_offset = 0;
    float scale = 1.0;

    image_rect_t src_box;
    src_box.left = 0;
    src_box.top = 0;
    src_box.right = src_image->width - 1;
    src_box.bottom = src_image->height - 1;

    image_rect_t dst_box;
    dst_box.left = 0;
    dst_box.top = 0;
    dst_box.right = dst_image->width - 1;
    dst_box.bottom = dst_image->height - 1;

    float _scale_w = (float)dst_w / src_w;
    float _scale_h = (float)dst_h / src_h;
    if(_scale_w < _scale_h) {
        scale = _scale_w;
        resize_h = (int) src_h*scale;
    } else {
        scale = _scale_h;
        resize_w = (int) src_w*scale;
    }
    // slight change image size for align
    if (allow_slight_change == 1 && (resize_w % 4 != 0)) {
        resize_w -= resize_w % 4;
    }
    if (allow_slight_change == 1 && (resize_h % 2 != 0)) {
        resize_h -= resize_h % 2;
    }
    // padding
    padding_h = dst_h - resize_h;
    padding_w = dst_w - resize_w;
    // center
    if (_scale_w < _scale_h) {
        dst_box.top = padding_h / 2;
        if (dst_box.top % 2 != 0) {
            dst_box.top -= dst_box.top % 2;
            if (dst_box.top < 0) {
                dst_box.top = 0;
            }
        }
        dst_box.bottom = dst_box.top + resize_h - 1;
        _top_offset = dst_box.top;
    } else {
        dst_box.left = padding_w / 2;
        if (dst_box.left % 2 != 0) {
            dst_box.left -= dst_box.left % 2;
            if (dst_box.left < 0) {
                dst_box.left = 0;
            }
        }
        dst_box.right = dst_box.left + resize_w - 1;
        _left_offset = dst_box.left;
    }
    printf("scale=%f dst_box=(%d %d %d %d) allow_slight_change=%d _left_offset=%d _top_offset=%d padding_w=%d padding_h=%d\n",
        scale, dst_box.left, dst_box.top, dst_box.right, dst_box.bottom, allow_slight_change,
        _left_offset, _top_offset, padding_w, padding_h);

    //set offset and scale
    if(letterbox != NULL){
        letterbox->scale = scale;
        letterbox->x_pad = _left_offset;
        letterbox->y_pad = _top_offset;
    }
    // alloc memory buffer for dst image,
    // remember to free
    if (dst_image->virt_addr == NULL && dst_image->fd <= 0) {
        int dst_size = get_image_size(dst_image);
        dst_image->virt_addr = (uint8_t *)malloc(dst_size);
        if (dst_image->virt_addr == NULL) {
            printf("malloc size %d error\n", dst_size);
            return -1;
        }
    }
    ret = convert_image(src_image, dst_image, &src_box, &dst_box, color);
    return ret;
}