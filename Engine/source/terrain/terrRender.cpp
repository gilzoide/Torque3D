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

//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
// Arcane-FX for MIT Licensed Open Source version of Torque 3D from GarageGames
// Copyright (C) 2015 Faust Logic, Inc.
//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//

#include "platform/platform.h"
#include "terrain/terrRender.h"

#include "terrain/terrData.h"
#include "terrain/terrCell.h"
#include "terrain/terrMaterial.h"
#include "terrain/terrCellMaterial.h"
#include "materials/shaderData.h"

#include "platform/profiler.h"
#include "scene/sceneRenderState.h"
#include "math/util/frustum.h"
#include "renderInstance/renderPassManager.h"
#include "renderInstance/renderTerrainMgr.h"

#include "lighting/lightInfo.h"
#include "lighting/lightManager.h"

#include "materials/matInstance.h"
#include "materials/materialManager.h"
#include "materials/matTextureTarget.h"
#include "shaderGen/conditionerFeature.h"

#include "gfx/gfxDrawUtil.h"

#ifdef TORQUE_AFX_ENABLED
#include "afx/arcaneFX.h"
#include "afx/ce/afxZodiacMgr.h"
#endif

#include "gfx/gfxTransformSaver.h"
#include "gfx/bitmap/gBitmap.h"
#include "gfx/bitmap/ddsFile.h"
#include "gfx/bitmap/imageUtils.h"
#include "terrain/terrMaterial.h"
#include "gfx/gfxDebugEvent.h"
#include "gfx/gfxCardProfile.h"
#include "core/stream/fileStream.h"


bool TerrainBlock::smDebugRender = false;


GFX_ImplementTextureProfile( TerrainLayerTexProfile,
                            GFXTextureProfile::DiffuseMap, 
                            GFXTextureProfile::PreserveSize | 
                            GFXTextureProfile::Dynamic,
                            GFXTextureProfile::NONE );


void TerrainBlock::_onFlushMaterials()
{
   if ( mCell )
      mCell->deleteMaterials();

   SAFE_DELETE( mBaseMaterial );
}

void TerrainBlock::_updateMaterials()
{
   if (!mFile)
      return;

   mBaseTextures.setSize( mFile->mMaterials.size() );

   mMaxDetailDistance = 0.0f;

   for ( U32 i=0; i < mFile->mMaterials.size(); i++ )
   {
      TerrainMaterial *mat = mFile->mMaterials[i];

      if (mat->getDiffuseMap() != StringTable->EmptyString())
      {
         mBaseTextures[i] = mat->getDiffuseMapResource();
      }
      else
         mBaseTextures[ i ] = GFXTexHandle();

      // Find the maximum detail distance.
      if (  mat->getDetailMap() != StringTable->EmptyString() &&
            mat->getDetailDistance() > mMaxDetailDistance )
         mMaxDetailDistance = mat->getDetailDistance();

      if (  mat->getMacroMap() != StringTable->EmptyString() &&
            mat->getMacroDistance() > mMaxDetailDistance )
         mMaxDetailDistance = mat->getMacroDistance();
   }

   Vector<GFXTexHandle> detailTexArray;
   detailTexArray.setSize(mFile->mMaterials.size());
   Vector<GFXTexHandle> macroTexArray;
   macroTexArray.setSize(mFile->mMaterials.size());
   Vector<GFXTexHandle> normalTexArray;
   normalTexArray.setSize(mFile->mMaterials.size());
   Vector<GFXTexHandle> ormTexArray;
   ormTexArray.setSize(mFile->mMaterials.size());

   for (U32 i = 0; i < mFile->mMaterials.size(); i++)
   {
      TerrainMaterial* mat = mFile->mMaterials[i];

      if (mat->getDetailMap() != StringTable->EmptyString())
         detailTexArray[i] = mat->getDetailMapResource();
      if (mat->getMacroMap() != StringTable->EmptyString())
         macroTexArray[i] = mat->getMacroMapResource();
      if (mat->getNormalMap() != StringTable->EmptyString())
         normalTexArray[i] = mat->getNormalMapResource();

      //depending on creation method this may or may not have been shoved into srgb space eroneously
      GFXTextureProfile* profile = &GFXStaticTextureProfile;
      if (mat->getIsSRGB())
         profile = &GFXStaticTextureSRGBProfile;
      if (mat->getORMConfigMap() != StringTable->EmptyString())
         ormTexArray[i] = TEXMGR->createTexture(mat->getORMConfigMap(), profile);
   }

   if (mDetailTextureArray.isNull())
   {
      mDetailTextureArray = GFX->createTextureArray();
   }

   if (mMacroTextureArray.isNull())
   {
      mMacroTextureArray = GFX->createTextureArray();
   }

   if (mNormalTextureArray.isNull())
   {
      mNormalTextureArray = GFX->createTextureArray();
   }

   if (mOrmTextureArray.isNull())
   {
      mOrmTextureArray = GFX->createTextureArray();
   }

   U32 detailTexArraySize = detailTexArray.size();
   U32 macroTexArraySize = macroTexArray.size();
   U32 normalTexArraySize = normalTexArray.size();
   U32 ormTexArraySize = ormTexArray.size();
#ifdef TORQUE_TOOLS
   // For performance improvement when adding terrain layers, we always allocate at least 32 textures to the arrays in tool builds
   detailTexArraySize = mMax(32, detailTexArraySize);
   macroTexArraySize = mMax(32, macroTexArraySize);
   normalTexArraySize = mMax(32, normalTexArraySize);
   ormTexArraySize = mMax(32, ormTexArraySize);
#endif

   // Format has been explicitly set
   const U32 detailTexSize = Con::getIntVariable("Terrain::DetailTextureSize");
   const GFXFormat detailTexFormat = static_cast<GFXFormat>(Con::getIntVariable("Terrain::DetailTextureFormat"));
   if (detailTexSize != 0)
   {
      GFXFormat format = GFXFormatR8G8B8A8;
      if (detailTexFormat < GFXFormat_COUNT)
      {
         format = detailTexFormat;
      }
      mDetailTextureArray->set(detailTexSize, detailTexSize, detailTexArraySize, format);
   }

   const U32 macroTexSize = Con::getIntVariable("Terrain::MacroTextureSize");
   const GFXFormat macroTexFormat = static_cast<GFXFormat>(Con::getIntVariable("Terrain::MacroTextureFormat"));
   if (macroTexSize != 0)
   {
      GFXFormat format = GFXFormatR8G8B8A8;
      if (macroTexFormat < GFXFormat_COUNT)
      {
         format = macroTexFormat;
      }
      mMacroTextureArray->set(macroTexSize, macroTexSize, macroTexArraySize, format);
   }

   const U32 normalTexSize = Con::getIntVariable("Terrain::NormalTextureSize");
   const GFXFormat normalTexFormat = static_cast<GFXFormat>(Con::getIntVariable("Terrain::NormalTextureFormat"));
   if (normalTexSize != 0)
   {
      GFXFormat format = GFXFormatR8G8B8A8;
      if (normalTexFormat < GFXFormat_COUNT)
      {
         format = normalTexFormat;
      }
      mNormalTextureArray->set(normalTexSize, normalTexSize, normalTexArraySize, format);
   }

   const U32 ormTexSize = Con::getIntVariable("Terrain::OrmTextureSize");
   const GFXFormat ormTexFormat = static_cast<GFXFormat>(Con::getIntVariable("Terrain::OrmTextureFormat"));
   if (ormTexSize != 0)
   {
      GFXFormat format = GFXFormatR8G8B8A8;
      if (ormTexFormat < GFXFormat_COUNT)
      {
         format = ormTexFormat;
      }
      mOrmTextureArray->set(ormTexSize, ormTexSize, ormTexArraySize, format);
   }

   if (!mDetailTextureArray->fromTextureArray(detailTexArray, detailTexArraySize))
   {
      Con::errorf("TerrainBlock::_updateMaterials - an issue with the detail terrain materials was detected. Please ensure they are all of the same size and format!");
   }

   if (!mMacroTextureArray->fromTextureArray(macroTexArray, macroTexArraySize))
   {
      Con::errorf("TerrainBlock::_updateMaterials - an issue with the macro terrain materials was detected. Please ensure they are all of the same size and format!");
   }

   if (!mNormalTextureArray->fromTextureArray(normalTexArray, normalTexArraySize))
   {
      Con::errorf("TerrainBlock::_updateMaterials - an issue with the normal terrain materials was detected. Please ensure they are all of the same size and format!");
   }

   if (!mOrmTextureArray->fromTextureArray(ormTexArray, ormTexArraySize))
   {
      Con::errorf("TerrainBlock::_updateMaterials - an issue with the orm terrain materials was detected. Please ensure they are all of the same size and format!");
   }

   if ( mCell )
      mCell->deleteMaterials();
}

void TerrainBlock::_updateLayerTexture()
{
   const U32 layerSize = mFile->mSize;
   const Vector<U8> &layerMap = mFile->mLayerMap;
   const U32 pixelCount = layerMap.size();

   if (  mLayerTex.isNull() ||
         mLayerTex.getWidth() != layerSize ||
         mLayerTex.getHeight() != layerSize )
      mLayerTex.set( layerSize, layerSize, GFXFormatB8G8R8A8, &TerrainLayerTexProfile, "" );

   AssertFatal(   mLayerTex.getWidth() == layerSize &&
                  mLayerTex.getHeight() == layerSize,
      "TerrainBlock::_updateLayerTexture - The texture size doesn't match the requested size!" );

   // Update the layer texture.
   GFXLockedRect *lock = mLayerTex.lock();

   for ( U32 i=0; i < pixelCount; i++ )
   {  
      lock->bits[0] = layerMap[i];

      if ( i + 1 >= pixelCount )
         lock->bits[1] = lock->bits[0];
      else
         lock->bits[1] = layerMap[i+1];

      if ( i + layerSize >= pixelCount )
         lock->bits[2] = lock->bits[0];
      else
         lock->bits[2] = layerMap[i + layerSize];

      if ( i + layerSize + 1 >= pixelCount )
         lock->bits[3] = lock->bits[0];
      else
         lock->bits[3] = layerMap[i + layerSize + 1];

      lock->bits += 4;
   }

   mLayerTex.unlock();
   //mLayerTex->dumpToDisk( "png", "./layerTex.png" );
}

bool TerrainBlock::_initBaseShader()
{
   ShaderData *shaderData = NULL;
   if ( !Sim::findObject( "TerrainBlendShader", shaderData ) || !shaderData )
      return false;

   mBaseShader = shaderData->getShader();

   mBaseShaderConsts = mBaseShader->allocConstBuffer();
   mBaseTexScaleConst = mBaseShader->getShaderConstHandle( "$texScale" );
   mBaseTexIdConst = mBaseShader->getShaderConstHandle( "$texId" );
   mBaseLayerSizeConst = mBaseShader->getShaderConstHandle( "$layerSize" );

   mBaseTarget = GFX->allocRenderToTextureTarget();

   GFXStateBlockDesc desc;
   desc.samplersDefined = true;
   desc.samplers[0] = GFXSamplerStateDesc::getClampPoint();   
   desc.samplers[1] = GFXSamplerStateDesc::getWrapLinear();   
   desc.zDefined = true;
   desc.zWriteEnable = false;
   desc.zEnable = false;
   desc.setBlend( true, GFXBlendSrcAlpha, GFXBlendOne  );
   desc.cullDefined = true;
   desc.cullMode = GFXCullNone;
   desc.colorWriteAlpha = false;
   mBaseShaderSB = GFX->createStateBlock( desc );

   return true;
}

void TerrainBlock::_updateBaseTexture(bool writeToCache)
{
   if ( !mBaseShader && !_initBaseShader() )
      return;

   // This can sometimes occur outside a begin/end scene.
   const bool sceneBegun = GFX->canCurrentlyRender();
   if ( !sceneBegun )
      GFX->beginScene();

   GFXDEBUGEVENT_SCOPE( TerrainBlock_UpdateBaseTexture, ColorI::GREEN );

   PROFILE_SCOPE( TerrainBlock_UpdateBaseTexture );

   GFXTransformSaver saver;

   const U32 maxTextureSize = GFX->getCardProfiler()->queryProfile( "maxTextureSize", 1024 );

   U32 baseTexSize = getNextPow2( mBaseTexSize );
   baseTexSize = getMin( maxTextureSize, baseTexSize );
   Point2I destSize( baseTexSize, baseTexSize );

   // Setup geometry
   GFXVertexBufferHandle<GFXVertexPT> vb;
   {
      F32 copyOffsetX = 2.0f * GFX->getFillConventionOffset() / (F32)destSize.x;
      F32 copyOffsetY = 2.0f * GFX->getFillConventionOffset() / (F32)destSize.y;

      GFXVertexPT points[4];
      points[0].point = Point3F(1.0 - copyOffsetX, -1.0 + copyOffsetY, 0.0);
      points[0].texCoord = Point2F(1.0, 1.0f);
      points[1].point = Point3F(1.0 - copyOffsetX, 1.0 + copyOffsetY, 0.0);
      points[1].texCoord = Point2F(1.0, 0.0f);
      points[2].point = Point3F(-1.0 - copyOffsetX, -1.0 + copyOffsetY, 0.0);
      points[2].texCoord = Point2F(0.0, 1.0f);
      points[3].point = Point3F(-1.0 - copyOffsetX, 1.0 + copyOffsetY, 0.0);
      points[3].texCoord = Point2F(0.0, 0.0f);

      vb.set( GFX, 4, GFXBufferTypeVolatile );
      GFXVertexPT *ptr = vb.lock();
      if(ptr)
      {
         dMemcpy( ptr, points, sizeof(GFXVertexPT) * 4 );
         vb.unlock();
      }
   }

   GFXTexHandle blendTex;

   // If the base texture is already a valid render target then 
   // use it to render to else we create one.
   if (  mBaseTex.isValid() && 
         mBaseTex->isRenderTarget() &&
         mBaseTex->getFormat() == GFXFormatR8G8B8A8_SRGB &&
         mBaseTex->getWidth() == destSize.x &&
         mBaseTex->getHeight() == destSize.y )
      blendTex = mBaseTex;
   else
      blendTex.set( destSize.x, destSize.y, GFXFormatR8G8B8A8_SRGB, &GFXRenderTargetSRGBProfile, "" );

   GFX->pushActiveRenderTarget();   

   // Set our shader stuff
   GFX->setShader( mBaseShader );
   GFX->setShaderConstBuffer( mBaseShaderConsts );
   GFX->setStateBlock( mBaseShaderSB );
   GFX->setVertexBuffer( vb );

   mBaseTarget->attachTexture( GFXTextureTarget::Color0, blendTex );
   GFX->setActiveRenderTarget( mBaseTarget );

   GFX->clear( GFXClearTarget, ColorI(0,0,0,255), 1.0f, 0 );

   GFX->setTexture( 0, mLayerTex );
   mBaseShaderConsts->setSafe( mBaseLayerSizeConst, (F32)mLayerTex->getWidth() );      

   for ( U32 i=0; i < mBaseTextures.size(); i++ )
   {
      GFXTextureObject *tex = mBaseTextures[i];
      if ( !tex )
         continue;

      GFX->setTexture( 1, tex );

      F32 baseSize = mFile->mMaterials[i]->getDiffuseSize();
      F32 scale = 1.0f;
      if ( !mIsZero( baseSize ) )
         scale = getWorldBlockSize() / baseSize;
      
      // A mistake early in development means that texture
      // coords are not flipped correctly.  To compensate
      // we flip the y scale here.
      mBaseShaderConsts->setSafe( mBaseTexScaleConst, Point2F( scale, -scale ) );
      mBaseShaderConsts->setSafe( mBaseTexIdConst, (F32)i );

      GFX->drawPrimitive( GFXTriangleStrip, 0, 2 );
   }

   mBaseTarget->resolve();
   
   GFX->setShader( NULL );
   //GFX->setStateBlock( NULL ); // WHY NOT?
   GFX->setShaderConstBuffer( NULL );
   GFX->setVertexBuffer( NULL );

   GFX->popActiveRenderTarget();

   // End it if we begun it... Yeehaw!
   if ( !sceneBegun )
      GFX->endScene();

   /// Do we cache this sucker?
   if (mBaseTexFormat == NONE || !writeToCache)
   {
      // We didn't cache the result, so set the base texture
      // to the render target we updated.  This should be good
      // for realtime painting cases.
      mBaseTex = blendTex;
   }
   else if (mBaseTexFormat == DDS)
   {
      String cachePath = _getBaseTexCacheFileName();

      FileStream fs;
      if ( fs.open( _getBaseTexCacheFileName(), Torque::FS::File::Write ) )
      {
         // Read back the render target, dxt compress it, and write it to disk.
         GBitmap blendBmp( destSize.x, destSize.y, false, GFXFormatR8G8B8A8 );
         blendTex.copyToBmp( &blendBmp );

         /*
         // Test code for dumping uncompressed bitmap to disk.
         {
         FileStream fs;
         if ( fs.open( "./basetex.png", Torque::FS::File::Write ) )
         {
         blendBmp.writeBitmap( "png", fs );
         fs.close();
         }         
         }
         */

         blendBmp.extrudeMipLevels();

         DDSFile *blendDDS = DDSFile::createDDSFileFromGBitmap( &blendBmp );
         ImageUtil::ddsCompress( blendDDS, GFXFormatBC1 );

         // Write result to file stream
         blendDDS->write( fs );
         
         delete blendDDS;
      }
      fs.close();
   }
   else
   {
      GBitmap bitmap(blendTex->getWidth(), blendTex->getHeight(), false, GFXFormatR8G8B8A8);
      blendTex->copyToBmp(&bitmap);
      bitmap.writeBitmap(formatToExtension(mBaseTexFormat), _getBaseTexCacheFileName());
   }
}

void TerrainBlock::_renderBlock( SceneRenderState *state )
{
   PROFILE_SCOPE( TerrainBlock_RenderBlock );

   if (!mFile)
      return;

   // Prevent rendering shadows if feature is disabled
   if ( !mCastShadows && state->isShadowPass() ) 
      return;
	  
   MatrixF worldViewXfm = state->getWorldViewMatrix();
   worldViewXfm.mul( getRenderTransform() );

   MatrixF worldViewProjXfm = state->getProjectionMatrix();
   worldViewProjXfm.mul( worldViewXfm );

   const MatrixF &objectXfm = getRenderWorldTransform();

   Point3F objCamPos = state->getDiffuseCameraPosition();
   objectXfm.mulP( objCamPos );

   // Get the shadow material.
   if ( !mDefaultMatInst )
      mDefaultMatInst = TerrainCellMaterial::getShadowMat();

   // Make sure we have a base material.
   if ( !mBaseMaterial )
   {
      mBaseMaterial = new TerrainCellMaterial();
      mBaseMaterial->init( this, 0, false, false, true );
   }

   // Did the detail layers change?
   if ( mDetailsDirty )
   {   
      _updateMaterials();
      mDetailsDirty = false;
   }

   // If the layer texture has been cleared or is 
   // dirty then update it.
   if ( mLayerTex.isNull() || mLayerTexDirty )
      _updateLayerTexture();

   // If the layer texture is dirty or we lost the base
   // texture then regenerate it.
   if ( mLayerTexDirty || mBaseTex.isNull() )
   {
      _updateBaseTexture( false );
      mLayerTexDirty = false;
   }   

   static Vector<TerrCell*> renderCells;
   renderCells.clear();

   mCell->cullCells( state,
                     objCamPos,
                     &renderCells );

   RenderPassManager *renderPass = state->getRenderPass();

   MatrixF *riObjectToWorldXfm = renderPass->allocUniqueXform( getRenderTransform() );

   const bool isColorDrawPass = state->isDiffusePass() || state->isReflectPass();

   // This is here for shadows mostly... it allows the
   // proper shadow material to be generated.
   BaseMatInstance *defaultMatInst = state->getOverrideMaterial( mDefaultMatInst );

   // Only pass and use the light manager if this is not a shadow pass.
   LightManager *lm = NULL;
   if ( isColorDrawPass )
      lm = LIGHTMGR;

#ifdef TORQUE_AFX_ENABLED
   bool has_zodiacs = afxZodiacMgr::doesBlockContainZodiacs(state, this);
#endif
   for ( U32 i=0; i < renderCells.size(); i++ )
   {
      TerrCell *cell = renderCells[i];

      // Ok this cell is fit to render.
      TerrainRenderInst *inst = renderPass->allocInst<TerrainRenderInst>();

      // Setup lights for this cell.
      if ( lm )
      {
         SphereF bounds = cell->getSphereBounds();
         getRenderTransform().mulP( bounds.center );

         LightQuery query;
         query.init( bounds );
		   query.getLights( inst->lights, 8 );
      }

      GFXVertexBufferHandleBase vertBuff;
      GFXPrimitiveBufferHandle  primBuff;

      cell->getRenderPrimitive( &inst->prim, &vertBuff, &primBuff );

      inst->mat = defaultMatInst;
      inst->vertBuff = vertBuff.getPointer();

      if ( primBuff.isValid() )
      {
         // Use the cell's custom primitive buffer
         inst->primBuff = primBuff.getPointer();
      }
      else
      {
         // Use the standard primitive buffer for this cell
         inst->primBuff = mPrimBuffer.getPointer();
      }

      inst->objectToWorldXfm = riObjectToWorldXfm;

      // If we're not drawing to the shadow map then we need
      // to include the normal rendering materials. 
      if ( isColorDrawPass )
      {         
         const SphereF &bounds = cell->getSphereBounds();         

         F32 sqDist = ( bounds.center - objCamPos ).lenSquared();         

         F32 radiusSq = mSquared( ( mMaxDetailDistance + bounds.radius ) * smDetailScale );

         // If this cell is near enough to get detail textures then
         // use the full detail mapping material.  Else we use the
         // simple base only material.
         if ( !state->isReflectPass() && sqDist < radiusSq )
            inst->cellMat = cell->getMaterial();
         else if ( state->isReflectPass() )
            inst->cellMat = mBaseMaterial->getReflectMat();
         else
            inst->cellMat = mBaseMaterial;
      }

      inst->defaultKey = (U32)cell->getMaterials();
#ifdef TORQUE_AFX_ENABLED
      if (has_zodiacs)
         afxZodiacMgr::renderTerrainZodiacs(state, this, cell);
      // Submit it for rendering.
#endif
      renderPass->addInst( inst );
   }

   // Trigger the debug rendering.
   if (  state->isDiffusePass() && 
         !renderCells.empty() && 
         smDebugRender )
   {      
      // Store the render cells for later.
      mDebugCells = renderCells;

      ObjectRenderInst *ri = state->getRenderPass()->allocInst<ObjectRenderInst>();
      ri->renderDelegate.bind( this, &TerrainBlock::_renderDebug );
      ri->type = RenderPassManager::RIT_Editor;
      state->getRenderPass()->addInst( ri );
   }
}

void TerrainBlock::_renderDebug( ObjectRenderInst *ri, 
                                 SceneRenderState *state, 
                                 BaseMatInstance *overrideMat )
{
   GFXTransformSaver saver;
   GFX->multWorld( getRenderTransform() );

   for ( U32 i=0; i < mDebugCells.size(); i++ )
      mDebugCells[i]->renderBounds();

   mDebugCells.clear();
}
