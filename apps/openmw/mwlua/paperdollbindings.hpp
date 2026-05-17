#ifndef OPENMW_MWLUA_PAPERDOLLBINDINGS_H
#define OPENMW_MWLUA_PAPERDOLLBINDINGS_H

#include <memory>
#include <string>

namespace osg
{
    class Group;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWWorld
{
    class Ptr;
}

namespace MyGUI
{
    class ITexture;
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
    /// Wraps an InventoryPreview (OSG render-to-texture of a character) so that it can be
    /// consumed by the Lua UI system as a dynamic TextureResource (ui.TYPE.Image, prop "resource").
    ///
    /// Lifetime note: this object must outlive any TextureResource that references it.
    /// In practice, uibindings.cpp stores a shared_ptr<LuaCharacterPreview> inside
    /// TextureData::mDynamicOwner to enforce this automatically.
    class LuaCharacterPreview
    {
    public:
        LuaCharacterPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
            const MWWorld::Ptr& actor);
        ~LuaCharacterPreview();

        /// Re-render the character (call after equipment changes).
        void update();

        /// Rotate the character model around the vertical axis (delegates to InventoryPreview).
        /// \param angleRadians  Yaw angle in radians.
        void setRotation(float angleRadians);

        /// Return the current yaw in radians (as set by the last setRotation call).
        float getRotation() const;

        /// Raw pointer to the MyGUI texture wrapping the OSG RTT result.
        /// Valid as long as this object is alive.
        MyGUI::ITexture* getMyGUITexture();

    private:
        std::unique_ptr<MWRender::InventoryPreview> mPreview;
        std::unique_ptr<MyGUIPlatform::OSGTexture> mOsgTexture;
    };

    /// Wraps an ObjectPreview (OSG render-to-texture of a static mesh) so that it can be
    /// consumed by the Lua UI system as a dynamic TextureResource (ui.TYPE.Image, prop "resource").
    ///
    /// Useful for displaying weapons, armor, misc items, or any object with a model,
    /// without requiring an NPC skeleton.
    class LuaObjectPreview
    {
    public:
        /// \param meshPath  Normalized VFS path, e.g. "meshes/w/w_longsword.nif".
        LuaObjectPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
            const std::string& meshPath);
        ~LuaObjectPreview();

        /// Re-render the mesh (e.g. after calling setRotations).
        void redraw();

        /// Rotate the mesh.
        /// \param yawRadians    Spin around the vertical (Z) axis, in radians.
        /// \param pitchRadians  Tilt around the lateral (X) axis, in radians.
        void setRotations(float yawRadians, float pitchRadians);

        float getYaw() const;
        float getPitch() const;

        /// Raw pointer to the MyGUI texture wrapping the OSG RTT result.
        /// Valid as long as this object is alive.
        MyGUI::ITexture* getMyGUITexture();

    private:
        std::unique_ptr<MWRender::ObjectPreview>   mPreview;
        std::unique_ptr<MyGUIPlatform::OSGTexture> mOsgTexture;
    };
}

#endif // OPENMW_MWLUA_PAPERDOLLBINDINGS_H
