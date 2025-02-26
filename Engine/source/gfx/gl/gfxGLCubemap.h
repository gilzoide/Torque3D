//-----------------------------------------------------------------------------
// Copyright (c) 2012 GarageGames, LLC
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
//-----------------------------------------------------------------------------

#ifndef _GFXGLCUBEMAP_H_
#define _GFXGLCUBEMAP_H_

#ifndef _GFXCUBEMAP_H_
#include "gfx/gfxCubemap.h"
#endif
#ifndef __RESOURCE_H__
#include "core/resource.h"
#endif

const U32 CubeFaces = 6;
const U32 MaxMipMaps = 13; //todo this needs a proper static value somewhere to sync up with other classes like GBitmap


class GFXGLCubemap : public GFXCubemap
{
public:
   GFXGLCubemap();
   virtual ~GFXGLCubemap();

   void initStatic( GFXTexHandle *faces ) override;
   void initStatic( DDSFile *dds ) override;
   void initDynamic( U32 texSize, GFXFormat faceFormat = GFXFormatR8G8B8A8, U32 mipLevels = 0) override;
   U32 getSize() const override { return mWidth; }
   GFXFormat getFormat() const override { return mFaceFormat; }

   bool isInitialized() override { return mCubemap != 0 ? true : false; }

   // Convenience methods for GFXGLTextureTarget
   U32 getWidth() { return mWidth; }
   U32 getHeight() { return mHeight; }
   U32 getHandle() { return mCubemap; }
   
   // GFXResource interface
   void zombify() override;
   void resurrect() override;
   
   /// Called by texCB; this is to ensure that all textures have been resurrected before we attempt to res the cubemap.
   void tmResurrect();
   
   static GLenum getEnumForFaceNumber(U32 face);///< Performs lookup to get a GLenum for the given face number

   /// @return An array containing the texture data
   /// @note You are responsible for deleting the returned data! (Use delete[])
   U8* getTextureData(U32 face, U32 mip = 0);

protected:

   friend class GFXDevice;
   friend class GFXGLDevice;

   /// The callback used to get texture events.
   /// @see GFXTextureManager::addEventDelegate
   void _onTextureEvent( GFXTexCallbackCode code );
   
   GLuint mCubemap; ///< Internal GL handle
   U32 mDynamicTexSize; ///< Size of faces for a dynamic texture (used in resurrect)
   
   // Self explanatory
   U32 mWidth;
   U32 mHeight;
   GFXFormat mFaceFormat;
      
   GFXTexHandle mTextures[6]; ///< Keep refs to our textures for resurrection of static cubemaps
   
   /// The backing DDSFile uses to restore the faces
   /// when the surface is lost.
   Resource<DDSFile> mDDSFile;

   // should only be called by GFXDevice
   void setToTexUnit( U32 tuNum ) override; ///< Binds the cubemap to the given texture unit
   virtual void bind(U32 textureUnit) const; ///< Notifies our owning device that we want to be set to the given texture unit (used for GL internal state tracking)
   void fillCubeTextures(GFXTexHandle* faces); ///< Copies the textures in faces into the cubemap
   
};

class GFXGLCubemapArray : public GFXCubemapArray
{
public:
   GFXGLCubemapArray();
   virtual ~GFXGLCubemapArray();
   //virtual void initStatic(GFXCubemapHandle *cubemaps, const U32 cubemapCount);
   void init(GFXCubemapHandle *cubemaps, const U32 cubemapCount) override;
   void init(const U32 cubemapCount, const U32 cubemapFaceSize, const GFXFormat format) override;
   void updateTexture(const GFXCubemapHandle &cubemap, const U32 slot) override;
   void copyTo(GFXCubemapArray *pDstCubemap) override;
   void setToTexUnit(U32 tuNum) override;

   // GFXResource interface
   void zombify() override {}
   void resurrect() override {}

protected:
   friend class GFXGLDevice;
   void bind(U32 textureUnit) const;
   GLuint mCubemap; ///< Internal GL handle

};

#endif
