#pragma once
//-----------------------------------------------------------------------------
// Copyright (c) 2013 GarageGames, LLC
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
#ifndef GUI_ASSET_H
#define GUI_ASSET_H

#ifndef _ASSET_BASE_H_
#include "assets/assetBase.h"
#endif

#ifndef _ASSET_DEFINITION_H_
#include "assets/assetDefinition.h"
#endif

#ifndef _STRINGUNIT_H_
#include "string/stringUnit.h"
#endif

#ifndef _ASSET_FIELD_TYPES_H_
#include "assets/assetFieldTypes.h"
#endif

#include "gui/editor/guiInspectorTypes.h"

//-----------------------------------------------------------------------------
class GUIAsset : public AssetBase
{
   typedef AssetBase Parent;

   StringTableEntry mScriptFile;
   StringTableEntry mGUIFile;

   StringTableEntry mScriptPath;
   StringTableEntry mGUIPath;

public:
   GUIAsset();
   virtual ~GUIAsset();

   /// Engine.
   static void initPersistFields();
   void copyTo(SimObject* object) override;

   static StringTableEntry getAssetIdByGUIName(StringTableEntry guiName);

   /// Declare Console Object.
   DECLARE_CONOBJECT(GUIAsset);

   void                    setGUIFile(const char* pScriptFile);
   inline StringTableEntry getGUIFile(void) const { return mGUIFile; };
   void                    setScriptFile(const char* pScriptFile);
   inline StringTableEntry getScriptFile(void) const { return mScriptFile; };

   inline StringTableEntry getGUIPath(void) const { return mGUIPath; };
   inline StringTableEntry getScriptPath(void) const { return mScriptPath; };

protected:
   void            initializeAsset(void) override;
   void            onAssetRefresh(void) override;

   static bool setGUIFile(void *obj, const char *index, const char *data) { static_cast<GUIAsset*>(obj)->setGUIFile(data); return false; }
   static const char* getGUIFile(void* obj, const char* data) { return static_cast<GUIAsset*>(obj)->getGUIFile(); }
   static bool setScriptFile(void *obj, const char *index, const char *data) { static_cast<GUIAsset*>(obj)->setScriptFile(data); return false; }
   static const char* getScriptFile(void* obj, const char* data) { return static_cast<GUIAsset*>(obj)->getScriptFile(); }
};

DefineConsoleType(TypeGUIAssetPtr, GUIAsset)

#ifdef TORQUE_TOOLS
//-----------------------------------------------------------------------------
// TypeAssetId GuiInspectorField Class
//-----------------------------------------------------------------------------
class GuiInspectorTypeGUIAssetPtr : public GuiInspectorTypeFileName
{
   typedef GuiInspectorTypeFileName Parent;
public:

   GuiBitmapButtonCtrl  *mSMEdButton;

   DECLARE_CONOBJECT(GuiInspectorTypeGUIAssetPtr);
   static void consoleInit();

   GuiControl* constructEditControl() override;
   bool updateRects() override;
};
#endif
#endif // _ASSET_BASE_H_

