#ifndef OPENMW_MWLUA_CHARACTERPREVIEWWRAPPER_H
#define OPENMW_MWLUA_CHARACTERPREVIEWWRAPPER_H

#include <memory>
#include <string>

#include <components/lua_ui/resources.hpp>

#include "../mwworld/ptr.hpp"

namespace osg
{
    class Group;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MyGUIPlatform
{
    class OSGTexture;
}

namespace MWRender
{
    class InventoryPreview;
    class ObjectPreview;
}

namespace MWLua
{
    // Wraps an MWRender::InventoryPreview so it can be exposed to Lua scripts as a
    // dynamic TextureResource consumable by ui.TYPE.Image.
    //
    // Threading model: every method that touches the OSG scene graph (mDoXxx) is meant to
    // be invoked from LuaManager::addAction, i.e. on the synchronized side of the frame.
    // The constructor itself does no OSG work — call doConstruct() from an addAction.
    //
    // Lifetime: the texture resource holds a shared_ptr<void> to the wrapper via
    // TextureData::mDynamicOwner, pinning it alive for as long as the texture is in use.
    // The wrapper holds only a weak_ptr to the texture resource to avoid a cycle.
    // Calling doDestroy() breaks the link.
    class LuaCharacterPreview
    {
    public:
        LuaCharacterPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
            const MWWorld::Ptr& actor, int sizeX, int sizeY);
        ~LuaCharacterPreview();

        void setTextureResource(const std::shared_ptr<LuaUi::TextureResource>& tr);
        std::shared_ptr<LuaUi::TextureResource> textureResource() const { return mTextureResource.lock(); }
        bool isDestroyed() const { return mDestroyed; }

        // Synchronous accessors (safe from any thread; no OSG access).
        float getRotation() const { return mRotation; }
        int getTextureWidth() const { return mSizeX; }
        int getTextureHeight() const { return mSizeY; }

        // OSG-mutating operations — call from a LuaManager::addAction.
        void doConstruct();
        void doUpdate();
        void doSetActor(const MWWorld::Ptr& newActor);
        void doSetRotation(float angleRadians);
        void doDestroy();

    private:
        osg::Group* mParent;
        Resource::ResourceSystem* mResourceSystem;
        MWWorld::Ptr mActor;
        int mSizeX;
        int mSizeY;
        float mRotation = 0.f;

        bool mDestroyed = false;

        std::weak_ptr<LuaUi::TextureResource> mTextureResource;

        std::unique_ptr<MWRender::InventoryPreview> mPreview;
        std::unique_ptr<MyGUIPlatform::OSGTexture> mOsgTexture;
    };

    // Wraps an MWRender::ObjectPreview the same way LuaCharacterPreview wraps InventoryPreview.
    // Works with any object that has a mesh; does NOT require an NPC skeleton.
    class LuaObjectPreview
    {
    public:
        LuaObjectPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
            std::string meshPath, int sizeX, int sizeY);
        ~LuaObjectPreview();

        void setTextureResource(const std::shared_ptr<LuaUi::TextureResource>& tr);
        std::shared_ptr<LuaUi::TextureResource> textureResource() const { return mTextureResource.lock(); }
        bool isDestroyed() const { return mDestroyed; }

        float getYaw() const { return mYaw; }
        float getPitch() const { return mPitch; }
        float getRoll() const { return mRoll; }
        int getTextureWidth() const { return mSizeX; }
        int getTextureHeight() const { return mSizeY; }

        void doConstruct();
        void doSetRotations(float yaw, float pitch, float roll);
        void doDestroy();

    private:
        osg::Group* mParent;
        Resource::ResourceSystem* mResourceSystem;
        std::string mMeshPath;
        int mSizeX;
        int mSizeY;

        float mYaw = 0.f;
        float mPitch = 0.f;
        float mRoll = 0.f;

        bool mDestroyed = false;

        std::weak_ptr<LuaUi::TextureResource> mTextureResource;

        std::unique_ptr<MWRender::ObjectPreview> mPreview;
        std::unique_ptr<MyGUIPlatform::OSGTexture> mOsgTexture;
    };
}

#endif // OPENMW_MWLUA_CHARACTERPREVIEWWRAPPER_H
