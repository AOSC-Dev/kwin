#include "workspacescene_vulkan.h"
#include "itemrenderer_vulkan.h"

namespace KWin
{

WorkspaceSceneVulkan::WorkspaceSceneVulkan(VulkanBackend *backend)
    : WorkspaceScene(std::make_unique<ItemRendererVulkan>(backend))
    , m_backend(backend)
{
}

WorkspaceSceneVulkan::~WorkspaceSceneVulkan()
{
}

}
