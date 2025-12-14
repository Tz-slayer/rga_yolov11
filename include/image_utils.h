#ifndef _RKNN_MODEL_ZOO_IMAGE_UTILS_H_
#define _RKNN_MODEL_ZOO_IMAGE_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

#define COLOR_RED    0xFF0000  // 红色
#define COLOR_GREEN  0x00FF00  // 绿色
#define COLOR_BLUE   0x0000FF  // 蓝色
#define COLOR_WHITE  0xFFFFFF  // 白色
#define COLOR_BLACK  0x000000  // 黑色
#define COLOR_YELLOW 0xFFFF00  // 黄色

/**
 * @brief LetterBox
 * 
 */
typedef struct {
    int x_pad;
    int y_pad;
    float scale;
} letterbox_t;

/**
 * @brief Read image file (support png/jpeg/bmp)
 * 
 * @param path [in] Image path
 * @param image [out] Read image
 * @return int 0: success; -1: error
 */
int read_image(const char* path, image_buffer_t* image);

/**
 * @brief Write image file (support jpg/png)
 * 
 * @param path [in] Image path
 * @param image [in] Image for write (only support IMAGE_FORMAT_RGB888)
 * @return int 0: success; -1: error
 */
int write_image(const char* path, const image_buffer_t* image);

/**
 * @brief Convert image for resize and pixel format change
 * 
 * @param src_image [in] Source Image
 * @param dst_image [out] Target Image
 * @param src_box [in] Crop rectangle on source image
 * @param dst_box [in] Crop rectangle on target image
 * @param color [in] Pading color if dst_box can not fill target image
 * @return int 
 */
int convert_image(image_buffer_t* src_image, image_buffer_t* dst_image, image_rect_t* src_box, image_rect_t* dst_box, char color);

/**
 * @brief Convert image with letterbox
 * 
 * @param src_image [in] Source Image
 * @param dst_image [out] Target Image
 * @param letterbox [out] Letterbox
 * @param color [in] Fill color on target image
 * @return int 
 */
int convert_image_with_letterbox(image_buffer_t* src_image, image_buffer_t* dst_image, letterbox_t* letterbox, char color);

/**
 * @brief Get the image size
 * 
 * @param image [in] Image
 * @return int image size
 */
int get_image_size(image_buffer_t* image);

int draw_rectangle(image_buffer_t* src_image, int x, int y, int width, int height, int color, int thickness);
int draw_text(image_buffer_t* src_image, const char* text, int x, int y, int color, int font_size);


#ifdef __cplusplus
}  // extern "C"
#endif

#endif // _RKNN_MODEL_ZOO_IMAGE_UTILS_H_