#include "image.hpp"

#include <MyGUI_ITexture.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_RotatingSkin.h>

#include <cmath>
#include <limits>

#include "resources.hpp"

namespace LuaUi
{
    void LuaTileRect::_setAlign(const MyGUI::IntSize& /*oldSize*/)
    {
        mCoord.set(0, 0, mCroppedParent->getWidth(), mCroppedParent->getHeight());
        mTileSize = mSetTileSize;

        // zero tilesize stands for not tiling
        if (mTileSize.width == 0)
            mTileSize.width = mCoord.width;
        if (mTileSize.height == 0)
            mTileSize.height = mCoord.height;

        // mCoord could be zero, prevent division by 0
        // use arbitrary large numbers to prevent performance issues
        if (mTileSize.width <= 0)
            mTileSize.width = 10000000;
        if (mTileSize.height <= 0)
            mTileSize.height = 10000000;

        MyGUI::TileRect::_updateView();
    }

    void LuaImage::initialize()
    {
        changeWidgetSkin("LuaImage");
        mTileRect = dynamic_cast<LuaTileRect*>(getSubWidgetMain());
        mRotating = false;
        WidgetExtension::initialize();
    }

    void LuaImage::setRotating(bool rotating)
    {
        if (rotating == mRotating)
            return;
        changeWidgetSkin(rotating ? "LuaImageRotating" : "LuaImage");
        // LuaTileRect only exists on the default skin; null while rotating.
        mTileRect = dynamic_cast<LuaTileRect*>(getSubWidgetMain());
        mRotating = rotating;
    }

    void LuaImage::applyRotation(float angle)
    {
        if (!mRotating)
            return;
        MyGUI::ISubWidget* main = getSubWidgetMain();
        if (main == nullptr)
            return;
        auto* rot = main->castType<MyGUI::RotatingSkin>(false);
        if (rot == nullptr)
            return;
        rot->setAngle(angle);
    }

    void LuaImage::updateCoord()
    {
        // Applies the staged position/size to the underlying widget (updateProperties only stages it).
        WidgetExtension::updateCoord();
        if (!mRotating)
            return;
        // Now that getSize() reflects the final size, set the rotation centre to the widget midpoint.
        // Doing this in updateProperties would use a stale size on the first build (the size is not yet
        // applied there), rotating the image around (0,0) and displacing it until the next refresh.
        MyGUI::ISubWidget* main = getSubWidgetMain();
        if (main == nullptr)
            return;
        auto* rot = main->castType<MyGUI::RotatingSkin>(false);
        if (rot == nullptr)
            return;
        const MyGUI::IntSize size = getSize();
        rot->setCenter(MyGUI::IntPoint(size.width / 2, size.height / 2));
    }

    void LuaImage::updateProperties()
    {
        // Decide rotation by PRESENCE of the property, not its value: a NaN sentinel means "absent".
        // Deciding by presence avoids a skin flip when the angle happens to pass through 0.
        const float rotation = propertyValue("rotation", std::numeric_limits<float>::quiet_NaN());
        setRotating(!std::isnan(rotation));

        // Dynamic texture path (e.g. character preview / map RTT texture).
        // Takes priority over the normal VFS-path resource.
        TextureResource* resource = propertyValue<TextureResource*>("resource", nullptr);
        if (resource && resource->mDynamicTexture)
        {
            setRenderItemTexture(resource->mDynamicTexture);
            getSubWidgetMain()->_setUVSet(resource->mFlipV
                    ? MyGUI::FloatRect(0.f, 1.f, 1.f, 0.f)
                    : MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));
            setColour(propertyValue("color", MyGUI::Colour(1, 1, 1, 1)));
            WidgetExtension::updateProperties();
            applyRotation(rotation);
            return;
        }

        // Reset UVs in case this widget previously displayed a flipped dynamic resource.
        getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));
        deleteAllItems();
        MyGUI::IntCoord atlasCoord;
        if (resource)
        {
            atlasCoord
                = MyGUI::IntCoord(static_cast<int>(resource->mOffset.x()), static_cast<int>(resource->mOffset.y()),
                    static_cast<int>(resource->mSize.x()), static_cast<int>(resource->mSize.y()));
            setImageTexture(resource->mPath);
        }

        bool tileH = propertyValue("tileH", false);
        bool tileV = propertyValue("tileV", false);

        MyGUI::ITexture* texture = MyGUI::RenderManager::getInstance().getTexture(_getTextureName());
        MyGUI::IntSize textureSize;
        if (texture != nullptr)
            textureSize = MyGUI::IntSize(texture->getWidth(), texture->getHeight());

        if (atlasCoord.width == 0)
            atlasCoord.width = textureSize.width;
        if (atlasCoord.height == 0)
            atlasCoord.height = textureSize.height;

        // Tiling is only available on the default skin (no LuaTileRect while rotating).
        if (mTileRect != nullptr)
            mTileRect->updateSize(MyGUI::IntSize(tileH ? atlasCoord.width : 0, tileV ? atlasCoord.height : 0));
        setImageTile(atlasCoord.size());
        setImageCoord(atlasCoord);

        setColour(propertyValue("color", MyGUI::Colour(1, 1, 1, 1)));

        // When rotating, the active sub-widget is a RotatingSkin. Push the resolved texture onto its
        // render item directly (as the dynamic-texture path does) so it is guaranteed to render,
        // regardless of how ImageBox routes its image methods to a non-TileRect main sub-widget.
        if (mRotating && texture != nullptr)
        {
            setRenderItemTexture(texture);
            getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));
        }

        WidgetExtension::updateProperties();

        // Angle only; the rotation centre is set in updateCoord, once the final size is applied.
        applyRotation(rotation);
    }

    const std::vector<std::string_view>& LuaImage::allUsedProperties() const
    {
        static std::vector<std::string_view> usedProps = std::invoke([this] {
            std::vector<std::string_view> props = { "resource", "tileH", "tileV", "color", "rotation" };
            auto baseProps = WidgetExtension::allUsedProperties();
            props.insert(props.end(), baseProps.begin(), baseProps.end());
            return props;
        });
        return usedProps;
    }
}
