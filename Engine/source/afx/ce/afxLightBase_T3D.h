
//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
// Arcane-FX for MIT Licensed Open Source version of Torque 3D from GarageGames
// Copyright (C) 2015 Faust Logic, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//

#ifndef _AFX_T3D_LIGHT_BASE_H_
#define _AFX_T3D_LIGHT_BASE_H_

#include "T3D/lightBase.h"

class LightAnimData;
class LightFlareData;

class afxT3DLightBaseData : public GameBaseData
{
  typedef GameBaseData  Parent;

public:
   bool         mIsEnabled;
   LinearColorF       mColor;
   F32          mBrightness;
   bool         mCastShadows;
   F32          mPriority;

   LightAnimData* mAnimationData;
   LightAnimState mAnimState;

   bool         mLocalRenderViz;

   LightFlareData* mFlareData;
   F32          mFlareScale;

   bool         do_id_convert;

public:
  /*C*/         afxT3DLightBaseData();
  /*C*/         afxT3DLightBaseData(const afxT3DLightBaseData&, bool = false);

  bool  onAdd() override;
  void  packData(BitStream*) override;
  void  unpackData(BitStream*) override;

  bool          preload(bool server, String &errorStr) override;

  static void   initPersistFields();

  DECLARE_CONOBJECT(afxT3DLightBaseData);
};

//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//

#endif // _AFX_T3D_LIGHT_BASE_H_
