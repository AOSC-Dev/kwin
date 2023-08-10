#pragma once

#include "platformsupport/scenes/vulkan/vulkan_backend.h"
#include "scene/workspacescene.h"

namespace KWin
{

class KWIN_EXPORT WorkspaceSceneVulkan : public WorkspaceScene
{
    Q_OBJECT
public:
    explicit WorkspaceSceneVulkan(VulkanBackend *backend);
    ~WorkspaceSceneVulkan() override;

    VulkanBackend *backend() const
    {
        return m_backend;
    }

private:
    VulkanBackend *const m_backend;
};

}
