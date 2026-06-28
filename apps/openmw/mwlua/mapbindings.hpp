#ifndef OPENMW_MWLUA_MAPBINDINGS_H
#define OPENMW_MWLUA_MAPBINDINGS_H

#include <memory>
#include <string>
#include <vector>

#include <osg/Vec2f>

#include <components/lua_ui/resources.hpp>

namespace osg
{
    class Texture2D;
}

namespace MyGUIPlatform
{
    class OSGTexture;
}

namespace MWRender
{
    class LocalMap;
    class GlobalMap;
}

namespace MWLua
{
    // Wraps MWRender::GlobalMap (the province / world map) so it can be exposed to Lua as two
    // dynamic TextureResources (the rendered base map + the explored overlay), plus a world->image
    // coordinate projection for placing markers.
    //
    // Lifetime mirrors the character-preview pattern: each published TextureResource co-owns the
    // OSGTexture wrapper through TextureData::mDynamicOwner, so the textures remain valid for as long
    // as any widget references them, independently of this object. doDestroy() releases them.
    class LuaWorldMap
    {
    public:
        explicit LuaWorldMap(MWRender::GlobalMap* globalMap);
        ~LuaWorldMap();

        void setBaseResource(const std::shared_ptr<LuaUi::TextureResource>& tr) { mBaseResource = tr; }
        void setOverlayResource(const std::shared_ptr<LuaUi::TextureResource>& tr) { mOverlayResource = tr; }

        std::shared_ptr<LuaUi::TextureResource> baseTexture() const { return mBaseResource.lock(); }
        std::shared_ptr<LuaUi::TextureResource> overlayTexture() const { return mOverlayResource.lock(); }

        // A discovered named location on the world map (world position = cell centre).
        struct Marker
        {
            std::string name;
            float x = 0.f;
            float y = 0.f;
        };

        int width() const;
        int height() const;
        osg::Vec2f worldToImage(float x, float y);
        std::vector<Marker> markers() const;
        float playerArrowAngle() const; // radians, ready for the Image `rotation` property

        // OSG-touching; call from a LuaManager::addAction.
        void doConstruct();
        void doDestroy();

    private:
        MWRender::GlobalMap* mGlobalMap;
        bool mDestroyed = false;

        std::shared_ptr<MyGUIPlatform::OSGTexture> mBaseOsg;
        std::shared_ptr<MyGUIPlatform::OSGTexture> mOverlayOsg;
        std::weak_ptr<LuaUi::TextureResource> mBaseResource;
        std::weak_ptr<LuaUi::TextureResource> mOverlayResource;
    };

    // Wraps MWRender::LocalMap (the current cell's map). Exposes the per-segment map + fog textures
    // the engine has already rendered around the player, a world->map projection, and an explored
    // test. Only segments the engine currently has rendered are returned (no on-demand rendering).
    class LuaLocalMap
    {
    public:
        // One renderable map tile: the segment grid coords and its map / fog texture resources.
        struct Segment
        {
            int x = 0;
            int y = 0;
            std::shared_ptr<LuaUi::TextureResource> mapTexture;
            std::shared_ptr<LuaUi::TextureResource> fogTexture; // may be null
        };

        // world (x, y) projected to a segment + normalized position within it.
        struct MapPosition
        {
            int segX = 0;
            int segY = 0;
            float nx = 0.f;
            float ny = 0.f;
        };

        LuaLocalMap(MWRender::LocalMap* localMap, LuaUi::ResourceManager* resourceManager);
        ~LuaLocalMap();

        // A teleport-door marker on the current cell's local map (world position).
        struct Door
        {
            std::string name;
            float x = 0.f;
            float y = 0.f;
        };

        bool isExterior() const;

        // Rebuilds the live segment list from what the engine currently has rendered around the
        // player. Reuses an internal resource pool, so the returned resources must not be retained
        // across calls.
        std::vector<Segment> segments();

        // Teleport-door markers for the player's current cell.
        std::vector<Door> doorMarkers() const;

        MapPosition worldToMap(float x, float y) const;
        float playerArrowAngle() const; // radians, ready for the Image `rotation` property
        bool isPositionExplored(int segX, int segY, float nx, float ny) const;

        void doDestroy();

    private:
        // A reusable pool entry: two registered resources, kept stable across refreshes so widgets
        // that hold them keep working. The OSGTextures themselves are never freed here (they may
        // still be referenced by a live widget); they are retained in mRetained until doDestroy.
        struct Slot
        {
            std::shared_ptr<LuaUi::TextureResource> mapResource;
            std::shared_ptr<LuaUi::TextureResource> fogResource;
        };

        Slot& acquireSlot(std::size_t index);

        MWRender::LocalMap* mLocalMap;
        LuaUi::ResourceManager* mResourceManager;
        std::vector<Slot> mPool;
        // Every OSGTexture ever published, retained so none is destroyed while a widget may still
        // be drawing it. Freed all at once in doDestroy (deferred to a safe point via addAction).
        std::vector<std::shared_ptr<MyGUIPlatform::OSGTexture>> mRetained;
        bool mDestroyed = false;
    };
}

#endif // OPENMW_MWLUA_MAPBINDINGS_H
