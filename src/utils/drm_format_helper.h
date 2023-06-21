/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2023 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once
#include <drm_fourcc.h>
#include <epoxy/gl.h>
#include <optional>
#include <stdint.h>
#define VULKAN_HPP_NO_EXCEPTIONS 1
#define VULKAN_HPP_ASSERT_ON_RESULT void
#include <vulkan/vulkan.hpp>

#include <QSysInfo>

struct FormatInfo
{
    uint32_t drmFormat;
    uint32_t bitsPerColor;
    uint32_t alphaBits;
    uint32_t bitsPerPixel;
    GLint openglFormat;
    vk::Format vulkanFormat;
};

static constexpr std::array s_knownDrmFormats{
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_XBGR8888,
    DRM_FORMAT_RGBX8888,
    DRM_FORMAT_BGRX8888,
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_ABGR8888,
    DRM_FORMAT_RGBA8888,
    DRM_FORMAT_BGRA8888,
    DRM_FORMAT_XRGB2101010,
    DRM_FORMAT_XBGR2101010,
    DRM_FORMAT_RGBX1010102,
    DRM_FORMAT_BGRX1010102,
    DRM_FORMAT_ARGB2101010,
    DRM_FORMAT_ABGR2101010,
    DRM_FORMAT_RGBA1010102,
    DRM_FORMAT_BGRA1010102,
    DRM_FORMAT_XRGB16161616F,
    DRM_FORMAT_ARGB16161616F,
    DRM_FORMAT_ABGR16161616F,
    DRM_FORMAT_ARGB4444,
    DRM_FORMAT_ABGR4444,
    DRM_FORMAT_RGBA4444,
    DRM_FORMAT_BGRA4444,
    DRM_FORMAT_ARGB1555,
    DRM_FORMAT_ABGR1555,
    DRM_FORMAT_RGBA5551,
    DRM_FORMAT_BGRA5551,
};

constexpr static std::optional<FormatInfo> formatInfo(uint32_t format)
{
    // list all known formats and their properties
    // Note that drm formats are little endian, and non-packed Vulkan formats are in memory byte order
    // so the order of components of non-packed formats is reversed
    static_assert(QSysInfo::ByteOrder == QSysInfo::LittleEndian);
    switch (format) {
    case DRM_FORMAT_XRGB8888:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 8,
            .alphaBits = 0,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGBA8,
            .vulkanFormat = vk::Format::eB8G8R8A8Unorm,
        };
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_BGRX8888:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 8,
            .alphaBits = 0,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGBA8,
            .vulkanFormat = vk::Format::eUndefined,
        };
    case DRM_FORMAT_ARGB8888:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 8,
            .alphaBits = 8,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGBA8,
            .vulkanFormat = vk::Format::eB8G8R8A8Unorm,
        };
    case DRM_FORMAT_ABGR8888:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 8,
            .alphaBits = 8,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGBA8,
            .vulkanFormat = vk::Format::eR8G8B8A8Unorm,
        };
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_BGRA8888:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 8,
            .alphaBits = 8,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGBA8,
            .vulkanFormat = vk::Format::eUndefined,
        };
    case DRM_FORMAT_XRGB2101010:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 10,
            .alphaBits = 0,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGB10_A2,
            .vulkanFormat = vk::Format::eA2R10G10B10UnormPack32,
        };
    case DRM_FORMAT_XBGR2101010:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 10,
            .alphaBits = 0,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGB10_A2,
            .vulkanFormat = vk::Format::eA2B10G10R10UnormPack32,
        };
    case DRM_FORMAT_RGBX1010102:
    case DRM_FORMAT_BGRX1010102:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 10,
            .alphaBits = 0,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGB10_A2,
            .vulkanFormat = vk::Format::eUndefined,
        };
    case DRM_FORMAT_ARGB2101010:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 10,
            .alphaBits = 2,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGB10_A2,
            .vulkanFormat = vk::Format::eA2R10G10B10UnormPack32,
        };
    case DRM_FORMAT_ABGR2101010:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 10,
            .alphaBits = 2,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGB10_A2,
            .vulkanFormat = vk::Format::eA2B10G10R10UnormPack32,
        };
    case DRM_FORMAT_RGBA1010102:
    case DRM_FORMAT_BGRA1010102:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 10,
            .alphaBits = 2,
            .bitsPerPixel = 32,
            .openglFormat = GL_RGB10_A2,
            .vulkanFormat = vk::Format::eUndefined,
        };
    case DRM_FORMAT_XRGB16161616F:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 16,
            .alphaBits = 0,
            .bitsPerPixel = 64,
            .openglFormat = GL_RGBA16F,
            .vulkanFormat = vk::Format::eUndefined,
        };
    case DRM_FORMAT_ARGB16161616F:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 16,
            .alphaBits = 16,
            .bitsPerPixel = 64,
            .openglFormat = GL_RGBA16F,
            .vulkanFormat = vk::Format::eUndefined,
        };
    case DRM_FORMAT_XBGR16161616F:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 16,
            .alphaBits = 0,
            .bitsPerPixel = 64,
            .openglFormat = GL_RGBA16F,
            .vulkanFormat = vk::Format::eR16G16B16A16Sfloat,
        };
    case DRM_FORMAT_ABGR16161616F:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 16,
            .alphaBits = 16,
            .bitsPerPixel = 64,
            .openglFormat = GL_RGBA16F,
            .vulkanFormat = vk::Format::eR16G16B16A16Sfloat,
        };
    case DRM_FORMAT_ARGB4444:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 4,
            .alphaBits = 4,
            .bitsPerPixel = 16,
            .openglFormat = GL_RGBA4,
            .vulkanFormat = vk::Format::eA4R4G4B4UnormPack16,
        };
    case DRM_FORMAT_ABGR4444:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 4,
            .alphaBits = 4,
            .bitsPerPixel = 16,
            .openglFormat = GL_RGBA4,
            .vulkanFormat = vk::Format::eA4B4G4R4UnormPack16,
        };
    case DRM_FORMAT_RGBA4444:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 4,
            .alphaBits = 4,
            .bitsPerPixel = 16,
            .openglFormat = GL_RGBA4,
            .vulkanFormat = vk::Format::eR4G4B4A4UnormPack16,
        };
    case DRM_FORMAT_BGRA4444:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 4,
            .alphaBits = 4,
            .bitsPerPixel = 16,
            .openglFormat = GL_RGBA4,
            .vulkanFormat = vk::Format::eB4G4R4A4UnormPack16,
        };
    case DRM_FORMAT_ARGB1555:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 5,
            .alphaBits = 1,
            .bitsPerPixel = 16,
            .openglFormat = GL_RGB5_A1,
            .vulkanFormat = vk::Format::eA1R5G5B5UnormPack16,
        };
    case DRM_FORMAT_ABGR1555:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 5,
            .alphaBits = 1,
            .bitsPerPixel = 16,
            .openglFormat = GL_RGB5_A1,
            .vulkanFormat = vk::Format::eUndefined,
        };
    case DRM_FORMAT_RGBA5551:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 5,
            .alphaBits = 1,
            .bitsPerPixel = 16,
            .openglFormat = GL_RGB5_A1,
            .vulkanFormat = vk::Format::eR5G5B5A1UnormPack16,
        };
    case DRM_FORMAT_BGRA5551:
        return FormatInfo{
            .drmFormat = format,
            .bitsPerColor = 5,
            .alphaBits = 1,
            .bitsPerPixel = 16,
            .openglFormat = GL_RGB5_A1,
            .vulkanFormat = vk::Format::eB5G5R5A1UnormPack16,
        };
    default:
        return std::nullopt;
    }
}
