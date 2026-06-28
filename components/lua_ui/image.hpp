#ifndef OPENMW_LUAUI_IMAGE
#define OPENMW_LUAUI_IMAGE

#include <vector>

#include <MyGUI_ImageBox.h>
#include <MyGUI_TileRect.h>

#include "widget.hpp"

namespace LuaUi
{
    class LuaTileRect : public MyGUI::TileRect
    {
        MYGUI_RTTI_DERIVED(LuaTileRect)

    public:
        void _setAlign(const MyGUI::IntSize& oldSize) override;

        void updateSize(MyGUI::IntSize tileSize) { mSetTileSize = tileSize; }

    protected:
        MyGUI::IntSize mSetTileSize;
    };

    class LuaImage : public MyGUI::ImageBox, public WidgetExtension
    {
        MYGUI_RTTI_DERIVED(LuaImage)

    protected:
        void initialize() override;
        void updateProperties() override;
        const std::vector<std::string_view>& allUsedProperties() const override;

        // Switches between the default tiling skin and the rotating skin, refreshing mTileRect
        // (null while rotating). No-op if already in the requested mode.
        void setRotating(bool rotating);
        // Sets the rotation centre (widget midpoint) and angle on the RotatingSkin. No-op unless rotating.
        void applyRotation(float angle);

        LuaTileRect* mTileRect = nullptr;
        bool mRotating = false;
    };
}

#endif // OPENMW_LUAUI_IMAGE
