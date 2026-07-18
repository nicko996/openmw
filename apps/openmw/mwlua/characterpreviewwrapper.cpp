#include "characterpreviewwrapper.hpp"

#include <osg/Texture2D>

#include <components/debug/debuglog.hpp>
#include <components/myguiplatform/myguitexture.hpp>

#include "../mwrender/characterpreview.hpp"

namespace MWLua
{
    namespace
    {
        void publishTextureToResource(const std::weak_ptr<LuaUi::TextureResource>& weakTr,
            MyGUIPlatform::OSGTexture* tex)
        {
            if (auto tr = weakTr.lock())
                tr->mDynamicTexture = tex;
        }

        void unpublishResource(const std::weak_ptr<LuaUi::TextureResource>& weakTr)
        {
            if (auto tr = weakTr.lock())
            {
                tr->mDynamicTexture = nullptr;
                tr->mDynamicOwner.reset();
            }
        }
    }

    // -- LuaCharacterPreview --------------------------------------------------------------------------

    LuaCharacterPreview::LuaCharacterPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
        const MWWorld::Ptr& actor, int sizeX, int sizeY)
        : mParent(parent)
        , mResourceSystem(resourceSystem)
        , mActor(actor)
        , mSizeX(sizeX)
        , mSizeY(sizeY)
    {
    }

    LuaCharacterPreview::~LuaCharacterPreview() = default;

    void LuaCharacterPreview::setTextureResource(const std::shared_ptr<LuaUi::TextureResource>& tr)
    {
        mTextureResource = tr;
    }

    void LuaCharacterPreview::doConstruct()
    {
        if (mDestroyed || mPreview)
            return;
        mPreview = std::make_unique<MWRender::InventoryPreview>(
            mParent, mResourceSystem, mActor, mSizeX, mSizeY);
        mPreview->rebuild();
        mPreview->update();

        mOsgTexture = std::make_unique<MyGUIPlatform::OSGTexture>(
            mPreview->getTexture().get(), mPreview->getTextureStateSet());

        if (mRotation != 0.f)
            mPreview->setRotation(mRotation);

        publishTextureToResource(mTextureResource, mOsgTexture.get());
    }

    void LuaCharacterPreview::doUpdate()
    {
        if (mDestroyed || !mPreview)
            return;
        mPreview->update();
    }

    void LuaCharacterPreview::doSetActor(const MWWorld::Ptr& newActor)
    {
        if (mDestroyed)
            return;
        mActor = newActor;
        if (mPreview)
        {
            mPreview->updatePtr(newActor);
            mPreview->rebuild();
            mPreview->update();
        }
    }

    void LuaCharacterPreview::doSetRotation(float angleRadians)
    {
        mRotation = angleRadians;
        if (mDestroyed || !mPreview)
            return;
        mPreview->setRotation(angleRadians);
    }

    void LuaCharacterPreview::doDestroy()
    {
        if (mDestroyed)
            return;
        mDestroyed = true;
        unpublishResource(mTextureResource);
        mOsgTexture.reset();
        mPreview.reset();
    }

}
