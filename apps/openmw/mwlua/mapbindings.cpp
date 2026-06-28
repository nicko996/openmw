#include "mapbindings.hpp"

#include <algorithm>
#include <cmath>

#include <MyGUI_Types.h>

#include <osg/Quat>
#include <osg/Texture2D>

#include <components/misc/constants.hpp>
#include <components/myguiplatform/myguitexture.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/cell.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/ptr.hpp"

#include "../mwrender/globalmap.hpp"
#include "../mwrender/localmap.hpp"

namespace MWLua
{
    namespace
    {
        // Publish an osg texture into a Lua texture resource. The resource co-owns the OSGTexture
        // (mDynamicOwner) so the texture survives for as long as a widget references it.
        // flipV matches the engine's own map window: the rendered map images (local map, world
        // base + overlay) are Y-up and must be flipped, but the fog-of-war texture is Y-down and
        // must NOT be flipped (see LocalMapBase::updateRequiredMaps / MapWindow::ensureGlobalMapLoaded).
        std::shared_ptr<MyGUIPlatform::OSGTexture> publish(
            const std::shared_ptr<LuaUi::TextureResource>& resource, osg::Texture2D* texture, bool flipV)
        {
            if (!resource || !texture)
                return nullptr;
            auto osgTexture = std::make_shared<MyGUIPlatform::OSGTexture>(texture);
            resource->mDynamicTexture = osgTexture.get();
            resource->mFlipV = flipV;
            resource->mDynamicOwner = osgTexture;
            return osgTexture;
        }

        void unpublish(const std::shared_ptr<LuaUi::TextureResource>& resource)
        {
            if (!resource)
                return;
            resource->mDynamicTexture = nullptr;
            resource->mDynamicOwner.reset();
        }

        struct PlayerCell
        {
            bool valid = false;
            bool exterior = false;
            int gridX = 0;
            int gridY = 0;
        };

        PlayerCell currentPlayerCell()
        {
            PlayerCell info;
            MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
            if (player.isEmpty())
                return info;
            const MWWorld::CellStore* store = player.getCell();
            if (store == nullptr)
                return info;
            info.valid = true;
            info.exterior = store->isExterior();
            const MWWorld::Cell* cell = store->getCell();
            if (cell != nullptr)
            {
                info.gridX = cell->getGridX();
                info.gridY = cell->getGridY();
            }
            return info;
        }

        // Mirrors MapWindow's getLocalViewingDistance(): how many cells around the player keep a
        // rendered local-map texture.
        int localViewingDistance()
        {
            if (!Settings::map().mAllowZooming)
                return Constants::CellGridRadius;
            if (!Settings::terrain().mDistantTerrain)
                return Constants::CellGridRadius;
            const int viewingDistanceInCells
                = static_cast<int>(Settings::camera().mViewingDistance / Constants::CellSizeInUnits);
            return std::clamp(
                viewingDistanceInCells, Constants::CellGridRadius, Settings::map().mMaxLocalViewingDistance.get());
        }
    }

    // -- LuaWorldMap --------------------------------------------------------------------------------

    LuaWorldMap::LuaWorldMap(MWRender::GlobalMap* globalMap)
        : mGlobalMap(globalMap)
    {
    }

    LuaWorldMap::~LuaWorldMap() = default;

    int LuaWorldMap::width() const
    {
        return mGlobalMap ? mGlobalMap->getWidth() : 0;
    }

    int LuaWorldMap::height() const
    {
        return mGlobalMap ? mGlobalMap->getHeight() : 0;
    }

    osg::Vec2f LuaWorldMap::worldToImage(float x, float y)
    {
        float imageX = 0.f;
        float imageY = 0.f;
        if (mGlobalMap)
            mGlobalMap->worldPosToImageSpace(x, y, imageX, imageY);
        return osg::Vec2f(imageX, imageY);
    }

    std::vector<LuaWorldMap::Marker> LuaWorldMap::markers() const
    {
        std::vector<Marker> out;
        for (const auto& m : MWBase::Environment::get().getWindowManager()->getDiscoveredMapMarkers())
            out.push_back({ m.mName, m.mX, m.mY });
        return out;
    }

    float LuaWorldMap::playerArrowAngle() const
    {
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return 0.f;
        // Same orientation construction as WindowManager::updateMap (yaw around Z).
        const float yaw = player.getRefData().getPosition().rot[2];
        osg::Quat orientation(-yaw, osg::Vec3(0, 0, 1));
        // World map is always north-up: no interior correction.
        osg::Vec3f dir = orientation * osg::Vec3f(0, 1, 0);
        return std::atan2(dir.x(), dir.y());
    }

    void LuaWorldMap::doConstruct()
    {
        if (mDestroyed || !mGlobalMap)
            return;
        // The province base map is generated lazily; getWidth() is 0 until it has been rendered.
        if (mGlobalMap->getWidth() == 0)
            mGlobalMap->render();
        mGlobalMap->ensureLoaded();

        mBaseOsg = publish(mBaseResource.lock(), mGlobalMap->getBaseTexture().get(), /*flipV=*/true);
        mOverlayOsg = publish(mOverlayResource.lock(), mGlobalMap->getOverlayTexture().get(), /*flipV=*/true);
    }

    void LuaWorldMap::doDestroy()
    {
        if (mDestroyed)
            return;
        mDestroyed = true;
        unpublish(mBaseResource.lock());
        unpublish(mOverlayResource.lock());
        mBaseOsg.reset();
        mOverlayOsg.reset();
    }

    // -- LuaLocalMap --------------------------------------------------------------------------------

    LuaLocalMap::LuaLocalMap(MWRender::LocalMap* localMap, LuaUi::ResourceManager* resourceManager)
        : mLocalMap(localMap)
        , mResourceManager(resourceManager)
    {
    }

    LuaLocalMap::~LuaLocalMap() = default;

    bool LuaLocalMap::isExterior() const
    {
        return currentPlayerCell().exterior;
    }

    LuaLocalMap::Slot& LuaLocalMap::acquireSlot(std::size_t index)
    {
        while (mPool.size() <= index)
        {
            Slot slot;
            slot.mapResource = mResourceManager->registerTexture({});
            slot.fogResource = mResourceManager->registerTexture({});
            mPool.push_back(std::move(slot));
        }
        return mPool[index];
    }

    std::vector<LuaLocalMap::Segment> LuaLocalMap::segments()
    {
        std::vector<Segment> result;
        if (mDestroyed || !mLocalMap)
            return result;

        const PlayerCell info = currentPlayerCell();
        if (!info.valid)
            return result;

        int left, top, right, bottom;
        if (info.exterior)
        {
            const int radius = localViewingDistance();
            left = info.gridX - radius;
            right = info.gridX + radius;
            top = info.gridY - radius;
            bottom = info.gridY + radius;
        }
        else
        {
            const MyGUI::IntRect grid = mLocalMap->getInteriorGrid();
            left = grid.left;
            top = grid.top;
            right = grid.right;
            bottom = grid.bottom;
        }

        std::size_t used = 0;
        for (int x = left; x <= right; ++x)
        {
            for (int y = top; y <= bottom; ++y)
            {
                osg::ref_ptr<osg::Texture2D> mapTexture = mLocalMap->getMapTexture(x, y);
                if (!mapTexture)
                    continue;
                osg::ref_ptr<osg::Texture2D> fogTexture = mLocalMap->getFogOfWarTexture(x, y);

                Slot& slot = acquireSlot(used);
                // Re-publish a fresh OSGTexture and retain it. The previously published texture (if
                // any) stays alive in mRetained, so a widget still drawing it never dangles.
                mRetained.push_back(publish(slot.mapResource, mapTexture.get(), /*flipV=*/true));

                Segment segment;
                segment.x = x;
                segment.y = y;
                segment.mapTexture = slot.mapResource;
                if (fogTexture)
                {
                    // The fog-of-war texture is Y-down in the engine, so it must NOT be flipped.
                    mRetained.push_back(publish(slot.fogResource, fogTexture.get(), /*flipV=*/false));
                    segment.fogTexture = slot.fogResource;
                }
                else
                {
                    unpublish(slot.fogResource);
                }
                result.push_back(std::move(segment));
                ++used;
            }
        }

        // Stop pointing unused pool slots at a texture (the OSGTextures stay alive in mRetained).
        for (std::size_t i = used; i < mPool.size(); ++i)
        {
            unpublish(mPool[i].mapResource);
            unpublish(mPool[i].fogResource);
        }

        return result;
    }

    LuaLocalMap::MapPosition LuaLocalMap::worldToMap(float x, float y) const
    {
        MapPosition pos;
        if (!mLocalMap)
            return pos;

        if (currentPlayerCell().exterior)
        {
            const float cellSize = static_cast<float>(Constants::CellSizeInUnits);
            pos.segX = static_cast<int>(std::ceil(x / cellSize) - 1);
            pos.segY = static_cast<int>(std::ceil(y / cellSize) - 1);
            pos.nx = std::abs((x - cellSize * pos.segX) / cellSize);
            pos.ny = 1.f - std::abs((y - cellSize * pos.segY) / cellSize);
        }
        else
        {
            mLocalMap->worldToInteriorMapPosition(osg::Vec2f(x, y), pos.nx, pos.ny, pos.segX, pos.segY);
        }
        return pos;
    }

    float LuaLocalMap::playerArrowAngle() const
    {
        if (!mLocalMap)
            return 0.f;
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return 0.f;
        // Same orientation construction as WindowManager::updateMap (yaw around Z).
        const float yaw = player.getRefData().getPosition().rot[2];
        osg::Quat orientation(-yaw, osg::Vec3(0, 0, 1));
        // getPlayerDirection applies the interior map-rotation correction when needed.
        osg::Vec3f dir = mLocalMap->getPlayerDirection(orientation);
        return std::atan2(dir.x(), dir.y());
    }

    std::vector<LuaLocalMap::Door> LuaLocalMap::doorMarkers() const
    {
        std::vector<Door> out;
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return out;
        MWWorld::CellStore* cell = player.getCell();
        if (cell == nullptr)
            return out;
        std::vector<MWBase::World::DoorMarker> doors;
        MWBase::Environment::get().getWorld()->getDoorMarkers(*cell, doors);
        out.reserve(doors.size());
        for (const auto& d : doors)
            out.push_back({ d.name, d.x, d.y });
        return out;
    }

    bool LuaLocalMap::isPositionExplored(int segX, int segY, float nx, float ny) const
    {
        if (!mLocalMap)
            return false;
        return mLocalMap->isPositionExplored(nx, ny, segX, segY);
    }

    void LuaLocalMap::doDestroy()
    {
        if (mDestroyed)
            return;
        mDestroyed = true;
        for (Slot& slot : mPool)
        {
            unpublish(slot.mapResource);
            unpublish(slot.fogResource);
        }
        mPool.clear();
        // Safe to free the OSGTextures now: doDestroy runs via a deferred LuaManager action, after
        // any widget using them has itself been destroyed.
        mRetained.clear();
    }
}
