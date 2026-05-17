#include "characterpreviewwrapper.hpp"

#include <osg/Texture2D>

#include <components/debug/debuglog.hpp>
#include <components/myguiplatform/myguitexture.hpp>

#include "../mwrender/characterpreview.hpp"
#include "../mwworld/ptr.hpp"

namespace MWLua
{
    namespace
    {
        // Sync the freshly-built OSG texture into the texture resource so that any
        // ui.TYPE.Image widget bound to the resource picks it up on its next update.
        // If the resource has already been released (weak_ptr expired) this is a no-op.
        void publishTextureToResource(const std::weak_ptr<LuaUi::TextureResource>& weakTr,
            MyGUIPlatform::OSGTexture* tex)
        {
            if (auto tr = weakTr.lock())
                tr->mDynamicTexture = tex;
        }

        // Detach the dynamic texture from the resource so widgets stop drawing it,
        // and drop the back-reference that pins the wrapper alive.
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

    // -- LuaObjectPreview -----------------------------------------------------------------------------

    LuaObjectPreview::LuaObjectPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
        std::string meshPath, int sizeX, int sizeY)
        : mParent(parent)
        , mResourceSystem(resourceSystem)
        , mMeshPath(std::move(meshPath))
        , mSizeX(sizeX)
        , mSizeY(sizeY)
    {
    }

    LuaObjectPreview::~LuaObjectPreview() = default;

    void LuaObjectPreview::setTextureResource(const std::shared_ptr<LuaUi::TextureResource>& tr)
    {
        mTextureResource = tr;
    }

    void LuaObjectPreview::doConstruct()
    {
        if (mDestroyed || mPreview)
            return;
        try
        {
            mPreview = std::make_unique<MWRender::ObjectPreview>(
                mParent, mResourceSystem, mMeshPath, mSizeX, mSizeY);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "newObjectPreview: " << e.what();
            mDestroyed = true;
            unpublishResource(mTextureResource);
            return;
        }

        mOsgTexture = std::make_unique<MyGUIPlatform::OSGTexture>(
            mPreview->getTexture().get(), mPreview->getTextureStateSet());

        if (mYaw != 0.f || mPitch != 0.f || mRoll != 0.f)
            mPreview->setRotations(mYaw, mPitch, mRoll);

        publishTextureToResource(mTextureResource, mOsgTexture.get());
    }

    void LuaObjectPreview::doSetRotations(float yaw, float pitch, float roll)
    {
        mYaw = yaw;
        mPitch = pitch;
        mRoll = roll;
        if (mDestroyed || !mPreview)
            return;
        mPreview->setRotations(yaw, pitch, roll);
    }

    void LuaObjectPreview::doDestroy()
    {
        if (mDestroyed)
            return;
        mDestroyed = true;
        unpublishResource(mTextureResource);
        mOsgTexture.reset();
        mPreview.reset();
    }
}
