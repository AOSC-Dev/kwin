#include "main.h"

#include <KPluginFactory>

#include "markerinterface.h"

using namespace KWin;

class KWIN_EXPORT CompatMarkerPluginFactory : public PluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "metadata.json")
    Q_INTERFACES(KWin::PluginFactory)

public:
    explicit CompatMarkerPluginFactory() = default;

    std::unique_ptr<Plugin> create() const override;
};

std::unique_ptr<Plugin> CompatMarkerPluginFactory::create() const
{
    return std::make_unique<CompatMarkerPlugin>();
}

#include "main.moc"
