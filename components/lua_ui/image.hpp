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
        // Sets the rotation centre to the widget midpoint once its size has been applied. The size is
        // only applied here (updateProperties merely stages it), so the centre must be set here or the
        // first build rotates around a stale (0,0) origin, displacing the image until the next refresh.
        void updateCoord() override;
        const std::vector<std::string_view>& allUsedProperties() const override;

        // Switches between the default tiling skin and the rotating skin, refreshing mTileRect
        // (null while rotating). No-op if already in the requested mode.
        void setRotating(bool rotating);
        // Sets the rotation angle on the RotatingSkin. No-op unless rotating. (The centre is set in
        // updateCoord, where the widget's final size is known.)
        void applyRotation(float angle);

        LuaTileRect* mTileRect = nullptr;
        bool mRotating = false;
    };
}

#endif // OPENMW_LUAUI_IMAGE
