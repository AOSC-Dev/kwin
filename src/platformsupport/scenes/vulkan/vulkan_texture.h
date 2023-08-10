/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2023 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once
#include "vulkan_include.h"

#include <QImage>

namespace KWin
{

class VulkanDevice;

class VulkanTexture
{
public:
    VulkanTexture(vk::Format format, vk::UniqueImage &&image, std::vector<vk::UniqueDeviceMemory> &&memory, vk::UniqueImageView &&imageView);
    VulkanTexture(const VulkanTexture &) = delete;
    VulkanTexture(VulkanTexture &&other);
    ~VulkanTexture();

    void transitionLayout(vk::CommandBuffer cmd, vk::ImageLayout newLayout);

    static std::unique_ptr<VulkanTexture> allocate(VulkanDevice *device, vk::Format format, const QSize &size);
    static std::unique_ptr<VulkanTexture> upload(VulkanDevice *device, vk::CommandBuffer cmd, const QImage &image);

private:
    vk::Format m_format;
    std::vector<vk::UniqueDeviceMemory> m_memory;
    vk::UniqueImage m_image;
    vk::UniqueImageView m_view;
    vk::ImageLayout m_layout = vk::ImageLayout::eUndefined;
};

}
