#ifndef QOI_FORMAT_CODEC_QOI_H_
#define QOI_FORMAT_CODEC_QOI_H_

#include "utils.h"

constexpr uint8_t QOI_OP_INDEX_TAG = 0x00;
constexpr uint8_t QOI_OP_DIFF_TAG  = 0x40;
constexpr uint8_t QOI_OP_LUMA_TAG  = 0x80;
constexpr uint8_t QOI_OP_RUN_TAG   = 0xc0;
constexpr uint8_t QOI_OP_RGB_TAG   = 0xfe;
constexpr uint8_t QOI_OP_RGBA_TAG  = 0xff;
constexpr uint8_t QOI_PADDING[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
constexpr uint8_t QOI_MASK_2 = 0xc0;

/**
 * @brief encode the raw pixel data of an image to qoi format.
 *
 * @param[in] width image width in pixels
 * @param[in] height image height in pixels
 * @param[in] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[in] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace = 0);

/**
 * @brief decode the qoi format of an image to raw pixel data
 *
 * @param[out] width image width in pixels
 * @param[out] height image height in pixels
 * @param[out] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[out] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace);


bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace) {
    if (width == 0 || height == 0 || (channels != 3 && channels != 4) ||
        colorspace > 1 || static_cast<uint64_t>(width) * height > 400000000ULL) {
        return false;
    }

    QoiWriteChar('q');
    QoiWriteChar('o');
    QoiWriteChar('i');
    QoiWriteChar('f');
    QoiWriteU32(width);
    QoiWriteU32(height);
    QoiWriteU8(channels);
    QoiWriteU8(colorspace);

    uint8_t history[64][4] = {};
    uint8_t previous[4] = {0u, 0u, 0u, 255u};
    uint8_t pixel[4] = {0u, 0u, 0u, 255u};
    int run = 0;
    const uint64_t pixel_count = static_cast<uint64_t>(width) * height;

    for (uint64_t i = 0; i < pixel_count; ++i) {
        pixel[0] = QoiReadU8();
        pixel[1] = QoiReadU8();
        pixel[2] = QoiReadU8();
        if (channels == 4) pixel[3] = QoiReadU8();
        if (!std::cin) return false;

        const bool same = pixel[0] == previous[0] && pixel[1] == previous[1] &&
                          pixel[2] == previous[2] && pixel[3] == previous[3];
        if (same) {
            ++run;
            if (run == 62 || i + 1 == pixel_count) {
                QoiWriteU8(static_cast<uint8_t>(QOI_OP_RUN_TAG | (run - 1)));
                run = 0;
            }
            continue;
        }

        if (run != 0) {
            QoiWriteU8(static_cast<uint8_t>(QOI_OP_RUN_TAG | (run - 1)));
            run = 0;
        }

        const int index = QoiColorHash(pixel[0], pixel[1], pixel[2], pixel[3]);
        if (history[index][0] == pixel[0] && history[index][1] == pixel[1] &&
            history[index][2] == pixel[2] && history[index][3] == pixel[3]) {
            QoiWriteU8(static_cast<uint8_t>(QOI_OP_INDEX_TAG | index));
        } else {
            memcpy(history[index], pixel, 4);
            if (pixel[3] != previous[3]) {
                QoiWriteU8(QOI_OP_RGBA_TAG);
                for (int component = 0; component < 4; ++component) {
                    QoiWriteU8(pixel[component]);
                }
            } else {
                // QOI differences wrap in the 8-bit color domain.
                const int dr = static_cast<int8_t>(pixel[0] - previous[0]);
                const int dg = static_cast<int8_t>(pixel[1] - previous[1]);
                const int db = static_cast<int8_t>(pixel[2] - previous[2]);
                if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 &&
                    db >= -2 && db <= 1) {
                    QoiWriteU8(static_cast<uint8_t>(QOI_OP_DIFF_TAG |
                        ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2)));
                } else {
                    const int dr_dg = dr - dg;
                    const int db_dg = db - dg;
                    if (dg >= -32 && dg <= 31 && dr_dg >= -8 && dr_dg <= 7 &&
                        db_dg >= -8 && db_dg <= 7) {
                        QoiWriteU8(static_cast<uint8_t>(QOI_OP_LUMA_TAG | (dg + 32)));
                        QoiWriteU8(static_cast<uint8_t>((dr_dg + 8) << 4 |
                                                        (db_dg + 8)));
                    } else {
                        QoiWriteU8(QOI_OP_RGB_TAG);
                        QoiWriteU8(pixel[0]);
                        QoiWriteU8(pixel[1]);
                        QoiWriteU8(pixel[2]);
                    }
                }
            }
        }
        memcpy(previous, pixel, 4);
    }

    for (uint8_t byte : QOI_PADDING) QoiWriteU8(byte);
    return static_cast<bool>(std::cout);
}

bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace) {
    const char c1 = QoiReadChar();
    const char c2 = QoiReadChar();
    const char c3 = QoiReadChar();
    const char c4 = QoiReadChar();
    if (!std::cin || c1 != 'q' || c2 != 'o' || c3 != 'i' || c4 != 'f') return false;

    width = QoiReadU32();
    height = QoiReadU32();
    channels = QoiReadU8();
    colorspace = QoiReadU8();
    const uint64_t pixel_count = static_cast<uint64_t>(width) * height;
    if (!std::cin || width == 0 || height == 0 || pixel_count > 400000000ULL ||
        (channels != 3 && channels != 4) || colorspace > 1) {
        return false;
    }

    uint8_t history[64][4] = {};
    uint8_t pixel[4] = {0u, 0u, 0u, 255u};
    int run = 0;

    for (uint64_t i = 0; i < pixel_count; ++i) {
        if (run != 0) {
            --run;
        } else {
            const uint8_t first = QoiReadU8();
            if (!std::cin) return false;

            if (first == QOI_OP_RGB_TAG) {
                pixel[0] = QoiReadU8();
                pixel[1] = QoiReadU8();
                pixel[2] = QoiReadU8();
            } else if (first == QOI_OP_RGBA_TAG) {
                for (int component = 0; component < 4; ++component) {
                    pixel[component] = QoiReadU8();
                }
            } else {
                switch (first & QOI_MASK_2) {
                    case QOI_OP_INDEX_TAG: {
                        const int index = first & 0x3f;
                        memcpy(pixel, history[index], 4);
                        break;
                    }
                    case QOI_OP_DIFF_TAG:
                        pixel[0] = static_cast<uint8_t>(pixel[0] + ((first >> 4) & 3) - 2);
                        pixel[1] = static_cast<uint8_t>(pixel[1] + ((first >> 2) & 3) - 2);
                        pixel[2] = static_cast<uint8_t>(pixel[2] + (first & 3) - 2);
                        break;
                    case QOI_OP_LUMA_TAG: {
                        const uint8_t second = QoiReadU8();
                        const int dg = (first & 0x3f) - 32;
                        pixel[0] = static_cast<uint8_t>(pixel[0] + dg + (second >> 4) - 8);
                        pixel[1] = static_cast<uint8_t>(pixel[1] + dg);
                        pixel[2] = static_cast<uint8_t>(pixel[2] + dg + (second & 0x0f) - 8);
                        break;
                    }
                    default:
                        run = first & 0x3f;
                        if (static_cast<uint64_t>(run) > pixel_count - i - 1) return false;
                        break;
                }
            }
            if (!std::cin) return false;
        }

        const int index = QoiColorHash(pixel[0], pixel[1], pixel[2], pixel[3]);
        memcpy(history[index], pixel, 4);
        QoiWriteU8(pixel[0]);
        QoiWriteU8(pixel[1]);
        QoiWriteU8(pixel[2]);
        if (channels == 4) QoiWriteU8(pixel[3]);
    }

    for (uint8_t expected : QOI_PADDING) {
        if (QoiReadU8() != expected || !std::cin) return false;
    }
    return static_cast<bool>(std::cout);
}

#endif // QOI_FORMAT_CODEC_QOI_H_
