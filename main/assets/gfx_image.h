/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GFX_IMAGE_FORMAT_RGB565_SWAPPED = 0,
} gfx_image_format_t;

typedef struct {
    const void* data;
    uint32_t data_size;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    gfx_image_format_t format;
} gfx_image_dsc_t;

#ifdef __cplusplus
}
#endif
