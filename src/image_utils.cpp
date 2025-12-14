#include "image_utils.h"
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

int draw_rectangle(image_buffer_t* src_image, int x, int y, int width, int height, int color, int thickness)
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
    // convert RGB to BGR if needed
    cv::Mat img_to_write;
    if (channels == 3) {
        cv::cvtColor(img, img_to_write, cv::COLOR_RGB2BGR);
    } else {
        img_to_write = img;
    }
    // save image
    if (!cv::imwrite(path, img_to_write)) {
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

int convert_image(image_buffer_t* src_img, image_buffer_t* dst_img, image_rect_t* src_box, image_rect_t* dst_box, char color)
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