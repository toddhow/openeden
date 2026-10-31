#ifndef OPENCS_VIEW_CELL_H
#define OPENCS_VIEW_CELL_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>

#include "../../model/world/cellcoordinates.hpp"
#include "instancedragmodes.hpp"
#include "worldspacewidget.hpp"
#include <components/esm/refid.hpp>
#include <components/misc/algorithm.hpp>

class QModelIndex;

namespace CSMWorld
{
    class Data;
}

namespace CSVRender
{
    class CellWater;
    class TagBase;
    class TerrainStorage;
    class Object;

    class CellArrow;
    class CellBorder;
    class CellMarker;

    class ObjectMarker;
    class SceneHost;

    /// What one cell shows in one view. Owned by its worldspace widget, and goes before the widget's SceneHost.
    class Cell : public FrameListener
    {
        SceneHost& mHost;
        ObjectMarker* const mSelectionMarker;
        CSMWorld::Data& mData;
        ESM::RefId mId;
        vsg::ref_ptr<vsg::Group> mCellNode;
        std::map<std::string, Object*, Misc::StringUtils::CiComp> mObjects;
        CSMWorld::CellCoordinates mCoordinates;
        std::unique_ptr<CellArrow> mCellArrows[4];
        bool mDeleted;
        int mSubMode;
        unsigned int mSubModeElementMask;
        bool mUpdateLand, mLandDeleted;

        /// Ignored if cell does not have an object with the given ID.
        ///
        /// \return Was the object deleted?
        bool removeObject(const std::string& id);

        // Remove object and return iterator to next object.
        std::map<std::string, Object*>::iterator removeObject(std::map<std::string, Object*>::iterator iter);

        /// Add objects from reference table that are within this cell.
        ///
        /// \return Have any objects been added?
        bool addObjects(int start, int end);

        void updateLand();
        void unloadLand();

    public:
        enum Selection
        {
            Selection_Clear,
            Selection_All,
            Selection_Invert
        };

    public:
        /// \note Deleted covers both cells that are deleted and cells that don't exist in
        /// the first place.
        Cell(CSMDoc::Document& document, ObjectMarker* selectionMarker, SceneHost& host, const std::string& id,
            bool deleted = false, bool isExterior = false);

        ~Cell() override;

        void frameUpdate(float dt) override;

        /// Where its objects are, for a default camera.
        std::vector<vsg::dvec3> getObjectPositions() const;

        /// Does something here move by itself, so that the view draws every tick?
        bool isAnimated() const { return !mObjects.empty(); }

        /// \return Did this call result in a modification of the visual representation of
        /// this cell?
        bool referenceableDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        /// \return Did this call result in a modification of the visual representation of
        /// this cell?
        bool referenceableAboutToBeRemoved(const QModelIndex& parent, int start, int end);

        /// \return Did this call result in a modification of the visual representation of
        /// this cell?
        bool referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        /// \return Did this call result in a modification of the visual representation of
        /// this cell?
        bool referenceAboutToBeRemoved(const QModelIndex& parent, int start, int end);

        /// \return Did this call result in a modification of the visual representation of
        /// this cell?
        bool referenceAdded(const QModelIndex& parent, int start, int end);

        // Stage C4 fills these in.
        void setAlteredHeight(int inCellX, int inCellY, float height) {}
        float getSumOfAlteredAndTrueHeight(int cellX, int cellY, int inCellX, int inCellY) { return 0.f; }
        float* getAlteredHeight(int inCellX, int inCellY) { return nullptr; }
        void resetAlteredHeights() {}

        // Stage C9: the pathgrid.
        void pathgridModified() {}
        void pathgridRemoved() {}

        void landDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        void landAboutToBeRemoved(const QModelIndex& parent, int start, int end);

        void landAdded(const QModelIndex& parent, int start, int end);

        void landTextureChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        void landTextureAboutToBeRemoved(const QModelIndex& parent, int start, int end);

        void landTextureAdded(const QModelIndex& parent, int start, int end);

        void reloadAssets();

        void setSelection(int elementMask, Selection mode);

        // Select everything that references the same ID as at least one of the elements
        // already selected
        void selectAllWithSameParentId(int elementMask);

        void selectFromGroup(const std::vector<std::string>& group);

        void unhideAll();

        void handleSelectDrag(Object* object, DragMode dragMode);

        void selectInsideCube(const vsg::dvec3& pointA, const vsg::dvec3& pointB, DragMode dragMode);

        void selectWithinDistance(const vsg::dvec3& pointA, float distance, DragMode dragMode);

        void setCellArrows(int mask);

        /// \brief Set marker for this cell.
        void setCellMarker();

        /// Returns 0, 0 in case of an unpaged cell.
        CSMWorld::CellCoordinates getCoordinates() const;

        bool isDeleted() const;

        vsg::ref_ptr<TagBase> getSnapTarget(unsigned int elementMask) const;

        std::vector<vsg::ref_ptr<TagBase>> getSelection(unsigned int elementMask) const;

        std::vector<vsg::ref_ptr<TagBase>> getEdited(unsigned int elementMask) const;

        void setSubMode(int subMode, unsigned int elementMask);

        /// Erase all overrides and restore the visual representation of the cell to its
        /// true state.
        void reset(unsigned int elementMask);

        CSVRender::Object* getObjectByReferenceId(const std::string& referenceId);

    };
}

#endif
