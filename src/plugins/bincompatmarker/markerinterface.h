#pragma once

#include "plugin.h"
#include "window.h"

namespace KWin
{
class CompatMarkerPlugin : public Plugin
{
    Q_OBJECT
public:
    explicit CompatMarkerPlugin();
    ~CompatMarkerPlugin() = default;
    void scanAll();

public Q_SLOTS:
    bool scanOne(const Window *window);
};
}
