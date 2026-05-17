#ifndef MWRENDER_CHARACTERPREVIEW_H
#define MWRENDER_CHARACTERPREVIEW_H

#include <memory>
#include <string>
#include <osg/ref_ptr>

#include <osg/PositionAttitudeTransform>

#include <components/esm3/loadnpc.hpp>

#include <components/resource/resourcesystem.hpp>

#include "../mwworld/ptr.hpp"

namespace osg
{
    class Texture2D;
    class Camera;
    class Group;
    class Viewport;
    class StateSet;
}

namespace MWRender
{

    class NpcAnimation;
    class DrawOnceCallback;
    class CharacterPreviewRTTNode;

    class CharacterPreview
    {
    public:
        CharacterPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem, const MWWorld::Ptr& character,
            int sizeX, int sizeY, const osg::Vec3f& position, const osg::Vec3f& lookAt);
        virtual ~CharacterPreview();

        int getTextureWidth() const;
        int getTextureHeight() const;

        void redraw();

        void rebuild();

        osg::ref_ptr<osg::Texture2D> getTexture();
        /// Get the osg::StateSet required to render the texture correctly, if any.
        osg::StateSet* getTextureStateSet() { return mTextureStateSet; }

    private:
        CharacterPreview(const CharacterPreview&);
        CharacterPreview& operator=(const CharacterPreview&);

    protected:
        virtual bool renderHeadOnly() { return false; }
        void setBlendMode();
        virtual void onSetup();

        osg::ref_ptr<osg::Group> mParent;
        Resource::ResourceSystem* mResourceSystem;
        osg::ref_ptr<osg::StateSet> mTextureStateSet;
        osg::ref_ptr<DrawOnceCallback> mDrawOnceCallback;
        osg::ref_ptr<CharacterPreviewRTTNode> mRTTNode;

        osg::Vec3f mPosition;
        osg::Vec3f mLookAt;

        MWWorld::Ptr mCharacter;

        osg::ref_ptr<MWRender::NpcAnimation> mAnimation;
        osg::ref_ptr<osg::PositionAttitudeTransform> mNode;
        std::string mCurrentAnimGroup;

        int mSizeX;
        int mSizeY;
    };

    class InventoryPreview : public CharacterPreview
    {
    public:
        InventoryPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem, const MWWorld::Ptr& character);
        InventoryPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem, const MWWorld::Ptr& character,
            int sizeX, int sizeY);

        void updatePtr(const MWWorld::Ptr& ptr);

        void update(); // Render preview again, e.g. after changed equipment
        void setViewport(int sizeX, int sizeY);

        int getSlotSelected(int posX, int posY);

        /// Rotate the character model around the vertical (Z) axis.
        /// \param angleRadians  Yaw in radians; positive = counter-clockwise when viewed from above.
        void setRotation(float angleRadians);

        /// Return the current yaw applied by setRotation, in radians.
        float getRotation() const { return mRotationRadians; }

    protected:
        osg::ref_ptr<osg::Viewport> mViewport;

        void onSetup() override;

    private:
        float mRotationRadians = 0.f;
    };

    /// RTT preview of a single static mesh (weapon, armor, misc item, etc.).
    /// Unlike CharacterPreview/InventoryPreview, does not require an NPC or a skeleton.
    /// Throws std::runtime_error if the mesh cannot be loaded.
    class ObjectPreview
    {
    public:
        /// \param parent          Root group node (same as CharacterPreview).
        /// \param resourceSystem
        /// \param meshPath        Normalized VFS path, e.g. "meshes/w/w_longsword.nif".
        /// \param sizeX           RTT texture width.
        /// \param sizeY           RTT texture height.
        ObjectPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
            const std::string& meshPath, int sizeX = 512, int sizeY = 512);
        ~ObjectPreview();

        /// Re-render (e.g. after calling setRotations).
        void redraw();

        /// Rotate the mesh.
        /// \param yawRadians    Spin around the vertical (Z) axis, in radians.
        /// \param pitchRadians  Tilt around the lateral (X) axis, in radians.
        /// \param rollRadians   Tilt around the forward (Y) axis, in radians.
        void setRotations(float yawRadians, float pitchRadians, float rollRadians = 0.f);

        float getYaw() const { return mYawRadians; }
        float getPitch() const { return mPitchRadians; }
        float getRoll() const { return mRollRadians; }

        int getTextureWidth() const { return mSizeX; }
        int getTextureHeight() const { return mSizeY; }

        osg::ref_ptr<osg::Texture2D> getTexture();
        /// Get the osg::StateSet required to render the texture correctly, if any.
        osg::StateSet* getTextureStateSet() { return mTextureStateSet; }

    private:
        osg::ref_ptr<osg::Group>                     mParent;
        Resource::ResourceSystem*                    mResourceSystem;
        osg::ref_ptr<osg::StateSet>                  mTextureStateSet;
        osg::ref_ptr<CharacterPreviewRTTNode>         mRTTNode;
        osg::ref_ptr<DrawOnceCallback>               mDrawOnceCallback;
        osg::ref_ptr<osg::PositionAttitudeTransform>  mNode;
        float mYawRadians   = 0.f;
        float mPitchRadians = 0.f;
        float mRollRadians  = 0.f;
        int mSizeX = 512;
        int mSizeY = 512;
    };

    class UpdateCameraCallback;

    class RaceSelectionPreview : public CharacterPreview
    {
        ESM::NPC mBase;
        MWWorld::LiveCellRef<ESM::NPC> mRef;

    protected:
        bool renderHeadOnly() override { return true; }
        void onSetup() override;

    public:
        RaceSelectionPreview(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        virtual ~RaceSelectionPreview();

        void setAngle(float angleRadians);

        const ESM::NPC& getPrototype() const { return mBase; }

        void setPrototype(const ESM::NPC& proto);

    private:
        osg::ref_ptr<UpdateCameraCallback> mUpdateCameraCallback;

        float mPitchRadians;
    };

}

#endif
