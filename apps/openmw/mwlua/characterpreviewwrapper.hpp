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
}

namespace MWLua
{
    // Wraps an MWRender::InventoryPreview so it can be exposed to Lua scripts as a
    // dynamic TextureResource consumable by ui.TYPE.Image.
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

}

#endif // OPENMW_MWLUA_CHARACTERPREVIEWWRAPPER_H
