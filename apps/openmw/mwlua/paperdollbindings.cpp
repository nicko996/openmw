#include "paperdollbindings.hpp"

#include <osg/Texture2D>

#include <components/myguiplatform/myguitexture.hpp>

#include "../mwrender/characterpreview.hpp"
#include "../mwworld/ptr.hpp"

namespace MWLua
{
    LuaCharacterPreview::LuaCharacterPreview(
        osg::Group* parent, Resource::ResourceSystem* resourceSystem, const MWWorld::Ptr& actor)
    {
        mPreview = std::make_unique<MWRender::InventoryPreview>(parent, resourceSystem, actor);
        mPreview->rebuild();
        mPreview->update();

        // Wrap the OSG RTT texture so MyGUI can consume it via setRenderItemTexture.
        mOsgTexture = std::make_unique<MyGUIPlatform::OSGTexture>(
            mPreview->getTexture().get(), mPreview->getTextureStateSet());
    }

    LuaCharacterPreview::~LuaCharacterPreview() = default;

    void LuaCharacterPreview::update()
    {
        if (mPreview)
            mPreview->update();
    }

    void LuaCharacterPreview::setRotation(float angleRadians)
    {
        if (mPreview)
            mPreview->setRotation(angleRadians);
    }

    float LuaCharacterPreview::getRotation() const
    {
        return mPreview ? mPreview->getRotation() : 0.f;
    }

    MyGUI::ITexture* LuaCharacterPreview::getMyGUITexture()
    {
        return mOsgTexture.get();
    }

    // --------------------------------------------------------------------------------------------------

    LuaObjectPreview::LuaObjectPreview(
        osg::Group* parent, Resource::ResourceSystem* resourceSystem, const std::string& meshPath)
    {
        mPreview = std::make_unique<MWRender::ObjectPreview>(parent, resourceSystem, meshPath);

        // Wrap the OSG RTT texture so MyGUI can consume it via setRenderItemTexture.
        mOsgTexture = std::make_unique<MyGUIPlatform::OSGTexture>(
            mPreview->getTexture().get(), mPreview->getTextureStateSet());
    }

    LuaObjectPreview::~LuaObjectPreview() = default;

    void LuaObjectPreview::redraw()
    {
        if (mPreview)
            mPreview->redraw();
    }

    void LuaObjectPreview::setRotations(float yawRadians, float pitchRadians)
    {
        if (mPreview)
            mPreview->setRotations(yawRadians, pitchRadians);
    }

    float LuaObjectPreview::getYaw() const
    {
        return mPreview ? mPreview->getYaw() : 0.f;
    }

    float LuaObjectPreview::getPitch() const
    {
        return mPreview ? mPreview->getPitch() : 0.f;
    }

    MyGUI::ITexture* LuaObjectPreview::getMyGUITexture()
    {
        return mOsgTexture.get();
    }
}
