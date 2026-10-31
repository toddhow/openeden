/*
  OpenMW - The completely unofficial reimplementation of Morrowind
  Copyright (C) 2008-2010  Nicolay Korslund
  Email: < korslund@gmail.com >
  WWW: https://openmw.org/

  This file (nif_types.h) is part of the OpenMW package.

  OpenMW is distributed as free software: you can redistribute it
  and/or modify it under the terms of the GNU General Public License
  version 3, as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  version 3 along with this program. If not, see
  https://www.gnu.org/licenses/ .

 */

#ifndef OPENMW_COMPONENTS_NIF_NIFTYPES_HPP
#define OPENMW_COMPONENTS_NIF_NIFTYPES_HPP

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/mat4.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

// Common types used in NIF files

namespace Nif
{

    struct Matrix3
    {
        float mValues[3][3];

        Matrix3()
        {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    mValues[i][j] = (i == j) ? 1.f : 0.f;
        }

        bool isIdentity() const
        {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    if ((i == j) != (mValues[i][j] == 1))
                        return false;
            return true;
        }

        vsg::mat4 toOsgMatrix() const
        {
            vsg::mat4 osgMat;

            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    osgMat[i][j] = mValues[j][i]; // NB: column/row major difference

            return osgMat;
        }
    };

    struct NiTransform
    {
        Matrix3 mRotation; // this can contain scale components too, including negative and nonuniform scales
        vsg::vec3 mTranslation;
        float mScale;

        vsg::mat4 toMatrix() const
        {
            vsg::mat4 transform;
            Misc::VsgMath::setTrans(transform, mTranslation);

            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    transform[j][i] = mRotation.mValues[i][j] * mScale; // NB column/row major difference

            return transform;
        }

        bool isIdentity() const { return mRotation.isIdentity() && mTranslation == vsg::vec3() && mScale == 1.f; }

        static const NiTransform& getIdentity()
        {
            static const NiTransform identity = { Matrix3(), vsg::vec3(), 1.0f };
            return identity;
        }
    };

    struct NiQuatTransform
    {
        vsg::vec3 mTranslation;
        vsg::dquat mRotation;
        float mScale;

        vsg::mat4 toMatrix() const
        {
            vsg::mat4 transform(Misc::VsgMath::toMatrix(mRotation));
            Misc::VsgMath::setTrans(transform, mTranslation);
            for (int i = 0; i < 3; i++)
                transform[i][i] *= mScale;

            return transform;
        }

        bool isIdentity() const { return mTranslation == vsg::vec3() && mRotation == vsg::dquat() && mScale == 1.f; }

        static const NiQuatTransform& getIdentity()
        {
            static const NiQuatTransform identity = { vsg::vec3(), vsg::dquat(), 1.f };
            return identity;
        }
    };

} // Namespace
#endif
