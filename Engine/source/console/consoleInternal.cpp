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

#include <unordered_map>

#include "platform/platform.h"
#include "console/console.h"

#include "core/tAlgorithm.h"

#include "core/strings/findMatch.h"
#include "console/consoleInternal.h"
#include "core/stream/fileStream.h"
#include "console/engineAPI.h"

//#define DEBUG_SPEW

#define ST_INIT_SIZE 15

static char scratchBuffer[1024];
U32 Namespace::mCacheSequence = 0;
DataChunker Namespace::mCacheAllocator;
DataChunker Namespace::mAllocator;
Namespace *Namespace::mNamespaceList = NULL;
Namespace *Namespace::mGlobalNamespace = NULL;

namespace std
{
   template<> struct hash<std::pair<StringTableEntry, StringTableEntry>>
   {
      typedef std::pair<StringTableEntry, StringTableEntry> argument_type;
      typedef size_t result_type;
      result_type operator()(argument_type const& s) const
      {
         return HashPointer(s.first) ^ (HashPointer(s.second) << 1);
      }
   };
};

std::unordered_map<std::pair<StringTableEntry, StringTableEntry>, Namespace*> gNamespaceCache;

bool canTabComplete(const char *prevText, const char *bestMatch,
   const char *newText, S32 baseLen, bool fForward)
{
   // test if it matches the first baseLen chars:
   if (dStrnicmp(newText, prevText, baseLen))
      return false;

   if (fForward)
   {
      if (!bestMatch)
         return dStricmp(newText, prevText) > 0;
      else
         return (dStricmp(newText, prevText) > 0) &&
         (dStricmp(newText, bestMatch) < 0);
   }
   else
   {
      if (dStrlen(prevText) == (U32)baseLen)
      {
         // look for the 'worst match'
         if (!bestMatch)
            return dStricmp(newText, prevText) > 0;
         else
            return dStricmp(newText, bestMatch) > 0;
      }
      else
      {
         if (!bestMatch)
            return (dStricmp(newText, prevText)  < 0);
         else
            return (dStricmp(newText, prevText)  < 0) &&
            (dStricmp(newText, bestMatch) > 0);
      }
   }
}

//---------------------------------------------------------------
//
// Dictionary functions
//
//---------------------------------------------------------------
struct StringValue
{
   S32 size;
   char *val;

   operator char *() { return val; }
   StringValue &operator=(const char *string);

   StringValue() { size = 0; val = NULL; }
   ~StringValue() { dFree(val); }
};


StringValue & StringValue::operator=(const char *string)
{
   if (!val)
   {
      val = dStrdup(string);
      size = dStrlen(val);
   }
   else
   {
      S32 len = dStrlen(string);
      if (len < size)
         dStrcpy(val, string, size);
      else
      {
         size = len;
         dFree(val);
         val = dStrdup(string);
      }
   }
   return *this;
}

static S32 QSORT_CALLBACK varCompare(const void* a, const void* b)
{
   return dStricmp((*((Dictionary::Entry **) a))->name, (*((Dictionary::Entry **) b))->name);
}

void Dictionary::exportVariables(const char *varString, const char *fileName, bool append)
{
   const char *searchStr = varString;
   Vector<Entry *> sortList(__FILE__, __LINE__);

   for (S32 i = 0; i < hashTable->size; i++)
   {
      Entry *walk = hashTable->data[i];
      while (walk)
      {
         if (FindMatch::isMatch((char *)searchStr, (char *)walk->name))
            sortList.push_back(walk);

         walk = walk->nextEntry;
      }
   }

   if (!sortList.size())
      return;

   dQsort((void *)&sortList[0], sortList.size(), sizeof(Entry *), varCompare);

   Vector<Entry *>::iterator s;
   char expandBuffer[1024];
   FileStream *strm = NULL;

   if (fileName)
   {
      if ((strm = FileStream::createAndOpen(fileName, append ? Torque::FS::File::ReadWrite : Torque::FS::File::Write)) == NULL)
      {
         Con::errorf(ConsoleLogEntry::General, "Unable to open file '%s for writing.", fileName);
         return;
      }
      if (append)
         strm->setPosition(strm->getStreamSize());
   }

   char buffer[1024];
   const char *cat = fileName ? "\r\n" : "";

   for (s = sortList.begin(); s != sortList.end(); s++)
   {
      switch ((*s)->value.getType())
      {
         case ConsoleValueType::cvInteger:
            dSprintf(buffer, sizeof(buffer), "%s = %d;%s", (*s)->name, (*s)->getIntValue(), cat);
            break;
         case ConsoleValueType::cvFloat:
            dSprintf(buffer, sizeof(buffer), "%s = %g;%s", (*s)->name, (*s)->getFloatValue(), cat);
            break;
         default:
            expandEscape(expandBuffer, (*s)->getStringValue());
            dSprintf(buffer, sizeof(buffer), "%s = \"%s\";%s", (*s)->name, expandBuffer, cat);
            break;
      }
      if (strm)
         strm->write(dStrlen(buffer), buffer);
      else
         Con::printf("%s", buffer);
   }
   if (strm)
      delete strm;
}

void Dictionary::exportVariables(const char *varString, Vector<String> *names, Vector<String> *values)
{
   const char *searchStr = varString;
   Vector<Entry *> sortList(__FILE__, __LINE__);

   for (S32 i = 0; i < hashTable->size; i++)
   {
      Entry *walk = hashTable->data[i];
      while (walk)
      {
         if (FindMatch::isMatch((char*)searchStr, (char*)walk->name))
            sortList.push_back(walk);

         walk = walk->nextEntry;
      }
   }

   if (!sortList.size())
      return;

   dQsort((void *)&sortList[0], sortList.size(), sizeof(Entry *), varCompare);

   if (names)
      names->reserve(sortList.size());
   if (values)
      values->reserve(sortList.size());

   char expandBuffer[1024];

   Vector<Entry *>::iterator s;

   for (s = sortList.begin(); s != sortList.end(); s++)
   {
      if (names)
         names->push_back(String((*s)->name));

      if (values)
      {
         switch ((*s)->value.getType())
         {
            case ConsoleValueType::cvInteger:
            case ConsoleValueType::cvFloat:
               values->push_back(String((*s)->getStringValue()));
               break;
            default:
               expandEscape(expandBuffer, (*s)->getStringValue());
               values->push_back(expandBuffer);
               break;
         }
      }
   }
}

void Dictionary::deleteVariables(const char *varString)
{
   const char *searchStr = varString;

   for (S32 i = 0; i < hashTable->size; i++)
   {
      Entry *walk = hashTable->data[i];
      while (walk)
      {
         Entry *matchedEntry = (FindMatch::isMatch((char *)searchStr, (char *)walk->name)) ? walk : NULL;
         walk = walk->nextEntry;
         if (matchedEntry)
            remove(matchedEntry); // assumes remove() is a stable remove (will not reorder entries on remove)
      }
   }
}

U32 HashPointer(StringTableEntry ptr)
{
   return (U32)(((dsize_t)ptr) >> 2);
}

Dictionary::Entry *Dictionary::lookup(StringTableEntry name)
{
   Entry *walk = hashTable->data[HashPointer(name) % hashTable->size];
   while (walk)
   {
      if (walk->name == name)
         return walk;
      else
         walk = walk->nextEntry;
   }

   return NULL;
}

Dictionary::Entry *Dictionary::add(StringTableEntry name)
{
   // Try to find an existing match.
   //printf("Add Variable %s\n", name);

   Entry* ret = lookup(name);
   if (ret)
      return ret;

   // Rehash if the table get's too crowded.  Be aware that this might
   // modify a table that we don't own.

   hashTable->count++;
   if (hashTable->count > hashTable->size * 2)
   {
      // Allocate a new table.

      const U32 newTableSize = hashTable->size * 4 - 1;
      Entry** newTableData = new Entry*[newTableSize];
      dMemset(newTableData, 0, newTableSize * sizeof(Entry*));

      // Move the entries over.

      for (U32 i = 0; i < hashTable->size; ++i)
         for (Entry* entry = hashTable->data[i]; entry != NULL; )
         {
            Entry* next = entry->nextEntry;
            U32 index = HashPointer(entry->name) % newTableSize;

            entry->nextEntry = newTableData[index];
            newTableData[index] = entry;

            entry = next;
         }

      // Switch the tables.

      delete[] hashTable->data;
      hashTable->data = newTableData;
      hashTable->size = newTableSize;
   }

#ifdef DEBUG_SPEW
   Platform::outputDebugString("[ConsoleInternal] Adding entry '%s'", name);
#endif

   // Add the new entry.

   ret = hashTable->mChunker.alloc();
   constructInPlace(ret, name);
   U32 idx = HashPointer(name) % hashTable->size;
   ret->nextEntry = hashTable->data[idx];
   hashTable->data[idx] = ret;

   return ret;
}

// deleteVariables() assumes remove() is a stable remove (will not reorder entries on remove)
void Dictionary::remove(Dictionary::Entry *ent)
{
   Entry **walk = &hashTable->data[HashPointer(ent->name) % hashTable->size];
   while (*walk != ent)
      walk = &((*walk)->nextEntry);

#ifdef DEBUG_SPEW
   Platform::outputDebugString("[ConsoleInternal] Removing entry '%s'", ent->name);
#endif

   *walk = (ent->nextEntry);

   hashTable->mChunker.free(ent);

   hashTable->count--;
}

Dictionary::Dictionary()
   : hashTable(NULL),
#pragma warning( disable : 4355 )
   ownHashTable(this), // Warning with VC++ but this is safe.
#pragma warning( default : 4355 )
   scopeName(NULL),
   scopeNamespace(NULL),
   module(NULL),
   ip(0)
{
   setState(NULL);
}

void Dictionary::setState(Dictionary* ref)
{
   if (ref)
   {
      hashTable = ref->hashTable;
      return;
   }

   if (!ownHashTable.data)
   {
      ownHashTable.count = 0;
      ownHashTable.size = ST_INIT_SIZE;
      ownHashTable.data = new Entry *[ownHashTable.size];

      dMemset(ownHashTable.data, 0, ownHashTable.size * sizeof(Entry*));
   }

   hashTable = &ownHashTable;
}

Dictionary::~Dictionary()
{
   reset();
   if (ownHashTable.data)
      delete[] ownHashTable.data;
}

void Dictionary::reset()
{
   if (hashTable && hashTable->owner != this)
   {
      hashTable = NULL;
      return;
   }

   for (U32 i = 0; i < ownHashTable.size; ++i)
   {
      Entry* walk = ownHashTable.data[i];
      while (walk)
      {
         Entry* temp = walk->nextEntry;
         destructInPlace(walk);
         walk = temp;
      }
   }

   dMemset(ownHashTable.data, 0, ownHashTable.size * sizeof(Entry*));
   ownHashTable.mChunker.freeBlocks(true);

   ownHashTable.count = 0;
   hashTable = NULL;

   scopeName = NULL;
   scopeNamespace = NULL;
   module = NULL;
   ip = 0;
}


const char *Dictionary::tabComplete(const char *prevText, S32 baseLen, bool fForward)
{
   S32 i;

   const char *bestMatch = NULL;
   for (i = 0; i < hashTable->size; i++)
   {
      Entry *walk = hashTable->data[i];
      while (walk)
      {
         if (canTabComplete(prevText, bestMatch, walk->name, baseLen, fForward))
            bestMatch = walk->name;
         walk = walk->nextEntry;
      }
   }
   return bestMatch;
}

Dictionary::Entry::Entry(StringTableEntry in_name)
{
   name = in_name;
   notify = NULL;
   nextEntry = NULL;
   mUsage = NULL;
   mIsConstant = false;
   mNext = NULL;
}

Dictionary::Entry::~Entry()
{
   reset();
}

void Dictionary::Entry::reset()
{
   name = NULL;
   value.reset();
   if (notify)
      delete notify;
}

const char *Dictionary::getVariable(StringTableEntry name, bool *entValid)
{
   Entry *ent = lookup(name);
   if (ent)
   {
      if (entValid)
         *entValid = true;
      return ent->getStringValue();
   }
   if (entValid)
      *entValid = false;

   // Warn users when they access a variable that isn't defined.
   if (gWarnUndefinedScriptVariables)
      Con::warnf(" *** Accessed undefined variable '%s'", name);

   return "";
}

S32 Dictionary::getIntVariable(StringTableEntry name, bool *entValid)
{
   Entry *ent = lookup(name);
   if (ent)
   {
      if (entValid)
         *entValid = true;
      return ent->getIntValue();
   }

   if (entValid)
      *entValid = false;

   return 0;
}

F32 Dictionary::getFloatVariable(StringTableEntry name, bool *entValid)
{
   Entry *ent = lookup(name);
   if (ent)
   {
      if (entValid)
         *entValid = true;
      return ent->getFloatValue();
   }

   if (entValid)
      *entValid = false;

   return 0;
}

void Dictionary::setVariable(StringTableEntry name, const char *value)
{
   Entry *ent = add(name);
   if (!value)
      value = "";
   ent->setStringValue(value);
}

Dictionary::Entry* Dictionary::addVariable(const char *name,
   S32 type,
   void *dataPtr,
   const char* usage)
{
   AssertFatal(type >= 0, "Dictionary::addVariable - Got bad type!");

   if (name[0] != '$')
   {
      scratchBuffer[0] = '$';
      dStrcpy(scratchBuffer + 1, name, 1023);
      name = scratchBuffer;
   }

   Entry *ent = add(StringTable->insert(name));

   ent->mUsage = usage;

   // Fetch enum table, if any.
   ConsoleBaseType* conType = ConsoleBaseType::getType(type);
   AssertFatal(conType, "Dictionary::addVariable - invalid console type");
   ent->value.setConsoleData(type, dataPtr, conType->getEnumTable());

   return ent;
}

bool Dictionary::removeVariable(StringTableEntry name)
{
   if (Entry *ent = lookup(name))
   {
      remove(ent);
      return true;
   }
   return false;
}

void Dictionary::addVariableNotify(const char *name, const Con::NotifyDelegate &callback)
{
   Entry *ent = lookup(StringTable->insert(name));
   if (!ent)
      return;

   if (!ent->notify)
      ent->notify = new Entry::NotifySignal();

   ent->notify->notify(callback);
}

void Dictionary::removeVariableNotify(const char *name, const Con::NotifyDelegate &callback)
{
   Entry *ent = lookup(StringTable->insert(name));
   if (ent && ent->notify)
      ent->notify->remove(callback);
}

void Dictionary::validate()
{
   AssertFatal(ownHashTable.owner == this,
      "Dictionary::validate() - Dictionary not owner of own hashtable!");
}

Con::Module* Con::findScriptModuleForFile(const char* fileName)
{
   for (Con::Module* module : gScriptModules)
   {
      if (module->getName() == fileName) {
         return module;
      }
   }
   return NULL;
}

DefineEngineFunction(backtrace, void, (), ,
   "@brief Prints the scripting call stack to the console log.\n\n"
   "Used to trace functions called from within functions. Can help discover what functions were called "
   "(and not yet exited) before the current point in scripts.\n\n"
   "@ingroup Debugging")
{
   U32 totalSize = 1;

   for (U32 i = 0; i < Con::getFrameStack().size(); i++)
   {
      const Con::ConsoleFrame* frame = Con::getStackFrame(i);
      if (frame->scopeNamespace && frame->scopeNamespace->mEntryList->mPackage)
         totalSize += dStrlen(frame->scopeNamespace->mEntryList->mPackage) + 2;
      if (frame->scopeName)
         totalSize += dStrlen(frame->scopeName) + 3;
      if (frame->scopeNamespace && frame->scopeNamespace->mName)
         totalSize += dStrlen(frame->scopeNamespace->mName) + 2;
   }

   char *buf = Con::getReturnBuffer(totalSize);
   buf[0] = 0;
   for (U32 i = 0; i < Con::getFrameStack().size(); i++)
   {
      const Con::ConsoleFrame* frame = Con::getStackFrame(i);
      dStrcat(buf, "->", totalSize);

      if (frame->scopeNamespace && frame->scopeNamespace->mEntryList->mPackage)
      {
         dStrcat(buf, "[", totalSize);
         dStrcat(buf, frame->scopeNamespace->mEntryList->mPackage, totalSize);
         dStrcat(buf, "]", totalSize);
      }
      if (frame->scopeNamespace && frame->scopeNamespace->mName)
      {
         dStrcat(buf, frame->scopeNamespace->mName, totalSize);
         dStrcat(buf, "::", totalSize);
      }
      if (frame->scopeName)
         dStrcat(buf, frame->scopeName, totalSize);
   }

   Con::printf("BackTrace: %s", buf);
}

Namespace::Entry::Entry()
{
   mModule = NULL;
   mType = InvalidFunctionType;
   mUsage = NULL;
   mHeader = NULL;
   mNamespace = NULL;
   cb.mStringCallbackFunc = NULL;
   mFunctionLineNumber = 0;
   mFunctionName = StringTable->EmptyString();
   mFunctionOffset = 0;
   mMinArgs = 0;
   mMaxArgs = 0;
   mNext = NULL;
   mPackage = StringTable->EmptyString();
   mToolOnly = false;
}

void Namespace::Entry::clear()
{
   if (mModule)
   {
      mModule->decRefCount();
      mModule = NULL;
   }

   // Clean up usage strings generated for script functions.
   if ((mType == Namespace::Entry::ConsoleFunctionType) && mUsage)
   {
      dFree(mUsage);
      mUsage = NULL;
   }
}

Namespace::Namespace()
{
   mPackage = NULL;
   mUsage = NULL;
   mCleanUpUsage = false;
   mName = NULL;
   mParent = NULL;
   mNext = NULL;
   mEntryList = NULL;
   mHashSize = 0;
   mHashTable = 0;
   mHashSequence = 0;
   mRefCountToParent = 0;
   mClassRep = 0;
   lastUsage = NULL;
}

Namespace::~Namespace()
{
   clearEntries();
   if (mUsage && mCleanUpUsage)
   {
      dFree(mUsage);
      mUsage = NULL;
      mCleanUpUsage = false;
   }
}

void Namespace::clearEntries()
{
   for (Entry *walk = mEntryList; walk; walk = walk->mNext)
      walk->clear();
}

Namespace *Namespace::find(StringTableEntry name, StringTableEntry package)
{
   if (name == NULL && package == NULL)
      return mGlobalNamespace;

   auto pair = std::make_pair(name, package);
   auto pos = gNamespaceCache.find(pair);
   if (pos != gNamespaceCache.end())
      return pos->second;

   Namespace *ret = (Namespace *)mAllocator.alloc(sizeof(Namespace));
   constructInPlace(ret);
   ret->mPackage = package;
   ret->mName = name;
   ret->mNext = mNamespaceList;
   mNamespaceList = ret;

   // insert into namespace cache.
   gNamespaceCache[pair] = ret;

   return ret;
}

bool Namespace::unlinkClass(Namespace *parent)
{
   AssertFatal(mPackage == NULL, "Namespace::unlinkClass - Must not be called on a namespace coming from a package!");

   // Skip additions to this namespace coming from packages.

   Namespace* walk = getPackageRoot();

   // Make sure "parent" is the direct parent namespace.

   if (parent != NULL && walk->mParent && walk->mParent != parent)
   {
      Con::errorf(ConsoleLogEntry::General, "Namespace::unlinkClass - cannot unlink namespace parent linkage for %s for %s.",
         walk->mName, walk->mParent->mName);
      return false;
   }

   // Decrease the reference count.  Note that we do this on
   // the bottom-most namespace, i.e. the one guaranteed not 
   // to come from a package.

   mRefCountToParent--;
   AssertFatal(mRefCountToParent >= 0, "Namespace::unlinkClass - reference count to parent is less than 0");

   // Unlink if the count dropped to zero.

   if (mRefCountToParent == 0)
   {
      walk->mParent = NULL;
      trashCache();
   }

   return true;
}


bool Namespace::classLinkTo(Namespace *parent)
{
   Namespace* walk = getPackageRoot();

   if (walk->mParent && walk->mParent != parent)
   {
      Con::errorf(ConsoleLogEntry::General, "Error: cannot change namespace parent linkage of %s from %s to %s.",
         walk->mName, walk->mParent->mName, parent->mName);
      return false;
   }

   trashCache();
   walk->mParent = parent;

   mRefCountToParent++;

   return true;
}

void Namespace::buildHashTable()
{
   if (mHashSequence == mCacheSequence)
      return;

   if (!mEntryList && mParent)
   {
      mParent->buildHashTable();
      mHashTable = mParent->mHashTable;
      mHashSize = mParent->mHashSize;
      mHashSequence = mCacheSequence;
      return;
   }

   U32 entryCount = 0;
   Namespace * ns;
   for (ns = this; ns; ns = ns->mParent)
      for (Entry *walk = ns->mEntryList; walk; walk = walk->mNext)
         if (lookupRecursive(walk->mFunctionName) == walk)
            entryCount++;

   mHashSize = entryCount + (entryCount >> 1) + 1;

   if (!(mHashSize & 1))
      mHashSize++;

   mHashTable = (Entry **)mCacheAllocator.alloc(sizeof(Entry *) * mHashSize);
   for (U32 i = 0; i < mHashSize; i++)
      mHashTable[i] = NULL;

   for (ns = this; ns; ns = ns->mParent)
   {
      for (Entry *walk = ns->mEntryList; walk; walk = walk->mNext)
      {
         U32 index = HashPointer(walk->mFunctionName) % mHashSize;
         while (mHashTable[index] && mHashTable[index]->mFunctionName != walk->mFunctionName)
         {
            index++;
            if (index >= mHashSize)
               index = 0;
         }

         if (!mHashTable[index])
            mHashTable[index] = walk;
      }
   }

   mHashSequence = mCacheSequence;
}

void Namespace::getUniqueEntryLists(Namespace *other, VectorPtr<Entry *> *outThisList, VectorPtr<Entry *> *outOtherList)
{
   // All namespace entries in the common ACR should be
   // ignored when checking for duplicate entry names.
   static VectorPtr<Namespace::Entry *> commonEntries;
   commonEntries.clear();

   AbstractClassRep *commonACR = mClassRep->getCommonParent(other->mClassRep);
   commonACR->getNameSpace()->getEntryList(&commonEntries);

   // Make life easier
   VectorPtr<Namespace::Entry *> &thisEntries = *outThisList;
   VectorPtr<Namespace::Entry *> &compEntries = *outOtherList;

   // Clear, just in case they aren't
   thisEntries.clear();
   compEntries.clear();

   getEntryList(&thisEntries);
   other->getEntryList(&compEntries);

   // Run through all of the entries in the common ACR, and remove them from
   // the other two entry lists
   for (NamespaceEntryListIterator itr = commonEntries.begin(); itr != commonEntries.end(); itr++)
   {
      // Check this entry list
      for (NamespaceEntryListIterator thisItr = thisEntries.begin(); thisItr != thisEntries.end(); thisItr++)
      {
         if (*thisItr == *itr)
         {
            thisEntries.erase(thisItr);
            break;
         }
      }

      // Same check for component entry list
      for (NamespaceEntryListIterator compItr = compEntries.begin(); compItr != compEntries.end(); compItr++)
      {
         if (*compItr == *itr)
         {
            compEntries.erase(compItr);
            break;
         }
      }
   }
}

void Namespace::init()
{
   // create the global namespace
   mGlobalNamespace = (Namespace *)mAllocator.alloc(sizeof(Namespace));
   constructInPlace(mGlobalNamespace);
   mGlobalNamespace->mPackage = NULL;
   mGlobalNamespace->mName = NULL;
   mGlobalNamespace->mNext = NULL;
   mNamespaceList = mGlobalNamespace;

   // Insert into namespace cache.
   gNamespaceCache[std::make_pair(mGlobalNamespace->mName, mGlobalNamespace->mPackage)] = mGlobalNamespace;
}

Namespace *Namespace::global()
{
   return mGlobalNamespace;
}

void Namespace::shutdown()
{
   // The data chunker will release all memory in one go
   // without calling destructors, so we do this manually here.

   for (Namespace *walk = mNamespaceList; walk; walk = walk->mNext)
      walk->~Namespace();
}

void Namespace::trashCache()
{
   mCacheSequence++;
   mCacheAllocator.freeBlocks();
}

const char *Namespace::tabComplete(const char *prevText, S32 baseLen, bool fForward)
{
   if (mHashSequence != mCacheSequence)
      buildHashTable();

   const char *bestMatch = NULL;
   for (U32 i = 0; i < mHashSize; i++)
      if (mHashTable[i] && canTabComplete(prevText, bestMatch, mHashTable[i]->mFunctionName, baseLen, fForward))
         bestMatch = mHashTable[i]->mFunctionName;
   return bestMatch;
}

Namespace::Entry *Namespace::lookupRecursive(StringTableEntry name)
{
   for (Namespace *ns = this; ns; ns = ns->mParent)
      for (Entry *walk = ns->mEntryList; walk; walk = walk->mNext)
         if (walk->mFunctionName == name)
            return walk;

   return NULL;
}

Namespace::Entry *Namespace::lookup(StringTableEntry name)
{
   if (mHashSequence != mCacheSequence)
      buildHashTable();

   U32 index = HashPointer(name) % mHashSize;
   while (mHashTable[index] && mHashTable[index]->mFunctionName != name)
   {
      index++;
      if (index >= mHashSize)
         index = 0;
   }
   return mHashTable[index];
}

static S32 QSORT_CALLBACK compareEntries(const void* a, const void* b)
{
   const Namespace::Entry* fa = *((Namespace::Entry**)a);
   const Namespace::Entry* fb = *((Namespace::Entry**)b);

   return dStricmp(fa->mFunctionName, fb->mFunctionName);
}

void Namespace::getEntryList(VectorPtr<Entry *> *vec)
{
   if (mHashSequence != mCacheSequence)
      buildHashTable();

   for (U32 i = 0; i < mHashSize; i++)
      if (mHashTable[i])
         vec->push_back(mHashTable[i]);

   dQsort(vec->address(), vec->size(), sizeof(Namespace::Entry *), compareEntries);
}

Namespace::Entry *Namespace::createLocalEntry(StringTableEntry name)
{
   for (Entry *walk = mEntryList; walk; walk = walk->mNext)
   {
      if (walk->mFunctionName == name)
      {
         walk->clear();
         return walk;
      }
   }

   Entry *ent = (Entry *)mAllocator.alloc(sizeof(Entry));
   constructInPlace(ent);

   ent->mNamespace = this;
   ent->mFunctionName = name;
   ent->mNext = mEntryList;
   ent->mPackage = mPackage;
   ent->mToolOnly = false;
   mEntryList = ent;
   return ent;
}

void Namespace::addFunction(StringTableEntry name, Con::Module *cb, U32 functionOffset, const char* usage, U32 lineNumber)
{
   Entry *ent = createLocalEntry(name);
   trashCache();

   ent->mUsage = usage;
   ent->mModule = cb;
   ent->mFunctionOffset = functionOffset;
   ent->mModule->incRefCount();
   ent->mType = Entry::ConsoleFunctionType;
   ent->mFunctionLineNumber = lineNumber;
}

void Namespace::addCommand(StringTableEntry name, StringCallback cb, const char *usage, S32 minArgs, S32 maxArgs, bool isToolOnly, ConsoleFunctionHeader* header)
{
   Entry *ent = createLocalEntry(name);
   trashCache();

   ent->mUsage = usage;
   ent->mHeader = header;
   ent->mMinArgs = minArgs;
   ent->mMaxArgs = maxArgs;
   ent->mToolOnly = isToolOnly;

   ent->mType = Entry::StringCallbackType;
   ent->cb.mStringCallbackFunc = cb;
}

void Namespace::addCommand(StringTableEntry name, IntCallback cb, const char *usage, S32 minArgs, S32 maxArgs, bool isToolOnly, ConsoleFunctionHeader* header)
{
   Entry *ent = createLocalEntry(name);
   trashCache();

   ent->mUsage = usage;
   ent->mHeader = header;
   ent->mMinArgs = minArgs;
   ent->mMaxArgs = maxArgs;
   ent->mToolOnly = isToolOnly;

   ent->mType = Entry::IntCallbackType;
   ent->cb.mIntCallbackFunc = cb;
}

void Namespace::addCommand(StringTableEntry name, VoidCallback cb, const char *usage, S32 minArgs, S32 maxArgs, bool isToolOnly, ConsoleFunctionHeader* header)
{
   Entry *ent = createLocalEntry(name);
   trashCache();

   ent->mUsage = usage;
   ent->mHeader = header;
   ent->mMinArgs = minArgs;
   ent->mMaxArgs = maxArgs;
   ent->mToolOnly = isToolOnly;

   ent->mType = Entry::VoidCallbackType;
   ent->cb.mVoidCallbackFunc = cb;
}

void Namespace::addCommand(StringTableEntry name, FloatCallback cb, const char *usage, S32 minArgs, S32 maxArgs, bool isToolOnly, ConsoleFunctionHeader* header)
{
   Entry *ent = createLocalEntry(name);
   trashCache();

   ent->mUsage = usage;
   ent->mHeader = header;
   ent->mMinArgs = minArgs;
   ent->mMaxArgs = maxArgs;
   ent->mToolOnly = isToolOnly;

   ent->mType = Entry::FloatCallbackType;
   ent->cb.mFloatCallbackFunc = cb;
}

void Namespace::addCommand(StringTableEntry name, BoolCallback cb, const char *usage, S32 minArgs, S32 maxArgs, bool isToolOnly, ConsoleFunctionHeader* header)
{
   Entry *ent = createLocalEntry(name);
   trashCache();

   ent->mUsage = usage;
   ent->mHeader = header;
   ent->mMinArgs = minArgs;
   ent->mMaxArgs = maxArgs;
   ent->mToolOnly = isToolOnly;

   ent->mType = Entry::BoolCallbackType;
   ent->cb.mBoolCallbackFunc = cb;
}

void Namespace::addScriptCallback(const char *funcName, const char *usage, ConsoleFunctionHeader* header)
{
   static U32 uid = 0;
   char buffer[1024];
   char lilBuffer[32];
   dStrcpy(buffer, funcName, 1024);
   dSprintf(lilBuffer, 32, "_%d_cb", uid++);
   dStrcat(buffer, lilBuffer, 1024);

   Entry *ent = createLocalEntry(StringTable->insert(buffer));
   trashCache();

   ent->mUsage = usage;
   ent->mHeader = header;
   ent->mMinArgs = -2;
   ent->mMaxArgs = -3;

   ent->mType = Entry::ScriptCallbackType;
   ent->cb.mCallbackName = funcName;
}

void Namespace::markGroup(const char* name, const char* usage)
{
   static U32 uid = 0;
   char buffer[1024];
   char lilBuffer[32];
   dStrcpy(buffer, name, 1024);
   dSprintf(lilBuffer, 32, "_%d", uid++);
   dStrcat(buffer, lilBuffer, 1024);

   Entry *ent = createLocalEntry(StringTable->insert(buffer));
   trashCache();

   if (usage != NULL)
      lastUsage = (char*)(ent->mUsage = usage);
   else
      ent->mUsage = lastUsage;

   ent->mMinArgs = -1; // Make sure it explodes if somehow we run this entry.
   ent->mMaxArgs = -2;

   ent->mType = Entry::GroupMarker;
   ent->cb.mGroupName = name;
}

ConsoleValue Namespace::Entry::execute(S32 argc, ConsoleValue *argv, SimObject *thisObj)
{
   STR.clearFunctionOffset();

   if (mType == ConsoleFunctionType)
   {
      if (mFunctionOffset)
      {
         return std::move(mModule->exec(mFunctionOffset, argv[0].getString(), mNamespace, argc, argv, false, mPackage).value);
      }
      else
      {
         return std::move(ConsoleValue());
      }
   }

#ifndef TORQUE_DEBUG
   // [tom, 12/13/2006] This stops tools functions from working in the console,
   // which is useful behavior when debugging so I'm ifdefing this out for debug builds.
   if (mToolOnly && !Con::isCurrentScriptToolScript())
   {
      Con::errorf(ConsoleLogEntry::Script, "%s::%s - attempting to call tools only function from outside of tools", mNamespace->mName, mFunctionName);
      return std::move(ConsoleValue());
   }
#endif

   if ((mMinArgs && argc < mMinArgs) || (mMaxArgs && argc > mMaxArgs))
   {
      Con::warnf(ConsoleLogEntry::Script, "%s::%s - wrong number of arguments.", mNamespace->mName, mFunctionName);
      Con::warnf(ConsoleLogEntry::Script, "usage: %s", mUsage);
      return std::move(ConsoleValue());
   }

   ConsoleValue result;
   switch (mType)
   {
      case StringCallbackType:
      {
         const char* str = cb.mStringCallbackFunc(thisObj, argc, argv);
         result.setString(str);
         break;
      }
      case IntCallbackType:
         result.setInt(cb.mIntCallbackFunc(thisObj, argc, argv));
         break;
      case FloatCallbackType:
         result.setFloat(cb.mFloatCallbackFunc(thisObj, argc, argv));
         break;
      case VoidCallbackType:
         cb.mVoidCallbackFunc(thisObj, argc, argv);
         break;
      case BoolCallbackType:
         result.setBool(cb.mBoolCallbackFunc(thisObj, argc, argv));
         break;
   }

   return result;
}

//-----------------------------------------------------------------------------
// Doc string code.

namespace {

   /// Scan the given usage string for an argument list description.  With the
   /// old console macros, these were usually included as the first part of the
   /// usage string.
   bool sFindArgumentListSubstring(const char* usage, const char*& start, const char*& end)
   {
      if (!usage)
         return false;

      const char* ptr = usage;
      while (*ptr && *ptr != '(' && *ptr != '\n') // Only scan first line of usage string.
      {
         // Stop on the first alphanumeric character as we expect
         // argument lists to precede descriptions.
         if (dIsalnum(*ptr))
            return false;

         ptr++;
      }

      if (*ptr != '(')
         return false;

      start = ptr;
      ptr++;

      bool inString = false;
      U32 nestingCount = 0;

      while (*ptr && (*ptr != ')' || nestingCount > 0 || inString))
      {
         if (*ptr == '(')
            nestingCount++;
         else if (*ptr == ')')
            nestingCount--;
         else if (*ptr == '"')
            inString = !inString;
         else if (*ptr == '\\' && ptr[1] == '"')
            ptr++;
         ptr++;
      }

      if (*ptr)
         ptr++;
      end = ptr;

      return true;
   }

   ///
   void sParseList(const char* str, Vector< String >& outList)
   {
      // Skip the initial '( '.

      const char* ptr = str;
      while (*ptr && dIsspace(*ptr))
         ptr++;

      if (*ptr == '(')
      {
         ptr++;
         while (*ptr && dIsspace(*ptr))
            ptr++;
      }

      // Parse out list items.

      while (*ptr && *ptr != ')')
      {
         // Find end of element.

         const char* start = ptr;

         bool inString = false;
         U32 nestingCount = 0;

         while (*ptr && ((*ptr != ')' && *ptr != ',') || nestingCount > 0 || inString))
         {
            if (*ptr == '(')
               nestingCount++;
            else if (*ptr == ')')
               nestingCount--;
            else if (*ptr == '"')
               inString = !inString;
            else if (*ptr == '\\' && ptr[1] == '"')
               ptr++;
            ptr++;
         }

         // Backtrack to remove trailing whitespace.

         const char* end = ptr;
         if (*end == ',' || *end == ')')
            end--;
         while (end > start && dIsspace(*end))
            end--;
         if (*end)
            end++;

         // Add to list.

         if (start != end)
            outList.push_back(String(start, end - start));

         // Skip comma and whitespace.

         if (*ptr == ',')
            ptr++;
         while (*ptr && dIsspace(*ptr))
            ptr++;
      }
   }

   ///
   void sGetArgNameAndType(const String& str, String& outType, String& outName)
   {
      if (!str.length())
      {
         outType = String::EmptyString;
         outName = String::EmptyString;
         return;
      }

      // Find first non-ID character from right.

      S32 index = str.length() - 1;
      while (index >= 0 && (dIsalnum(str[index]) || str[index] == '_'))
         index--;

      const U32 nameStartIndex = index + 1;

      // Find end of type name by skipping rightmost whitespace inwards.

      while (index >= 0 && dIsspace(str[index]))
         index--;

      //

      outName = String(&((const char*)str)[nameStartIndex]);
      outType = String(str, index + 1);
   }

   /// Return the type name to show in documentation for the given C++ type.
   const char* sGetDocTypeString(const char* nativeType)
   {
      if (dStrncmp(nativeType, "const ", 6) == 0)
         nativeType += 6;

      if (String::compare(nativeType, "char*") == 0 || String::compare(nativeType, "char *") == 0)
         return "string";
      else if (String::compare(nativeType, "S32") == 0)
         return "int";
      else if (String::compare(nativeType, "U32") == 0)
         return "uint";
      else if (String::compare(nativeType, "F32") == 0)
         return "float";

      const U32 length = dStrlen(nativeType);
      if (nativeType[length - 1] == '&' || nativeType[length - 1] == '*')
         return StringTable->insertn(nativeType, length - 1);

      return nativeType;
   }
}

String Namespace::Entry::getBriefDescription(String* outRemainingDocText) const
{
   String docString = getDocString();

   S32 newline = docString.find('\n');
   if (newline == -1)
   {
      if (outRemainingDocText)
         *outRemainingDocText = String();
      return docString;
   }

   String brief = docString.substr(0, newline);
   if (outRemainingDocText)
      *outRemainingDocText = docString.substr(newline + 1);

   return brief;
}

String Namespace::Entry::getDocString() const
{
   const char* argListStart;
   const char* argListEnd;

   if (sFindArgumentListSubstring(mUsage, argListStart, argListEnd))
   {
      // Skip the " - " part present in some old doc strings.

      const char* ptr = argListEnd;
      while (*ptr && dIsspace(*ptr))
         ptr++;

      if (*ptr == '-')
      {
         ptr++;
         while (*ptr && dIsspace(*ptr))
            ptr++;
      }

      return ptr;
   }

   return mUsage;
}

String Namespace::Entry::getArgumentsString() const
{
   StringBuilder str;

   if (mHeader)
   {
      // Parse out the argument list string supplied with the extended
      // function header and add default arguments as we go.

      Vector< String > argList;
      Vector< String > defaultArgList;

      sParseList(mHeader->mArgString, argList);
      sParseList(mHeader->mDefaultArgString, defaultArgList);

      str.append('(');

      const U32 numArgs = argList.size();
      const U32 numDefaultArgs = defaultArgList.size();
      const U32 firstDefaultArgIndex = numArgs - numDefaultArgs;

      for (U32 i = 0; i < numArgs; ++i)
      {
         // Add separator if not first arg.

         if (i > 0)
            str.append(',');

         // Add type and name.

         String name;
         String type;

         sGetArgNameAndType(argList[i], type, name);

         str.append(' ');
         str.append(sGetDocTypeString(type));
         str.append(' ');
         str.append(name);

         // Add default value, if any.

         if (i >= firstDefaultArgIndex)
         {
            str.append('=');
            str.append(defaultArgList[i - firstDefaultArgIndex]);
         }
      }

      if (numArgs > 0)
         str.append(' ');
      str.append(')');
   }
   else
   {
      // No extended function header.  Try to parse out the argument
      // list from the usage string.

      const char* argListStart;
      const char* argListEnd;

      if (sFindArgumentListSubstring(mUsage, argListStart, argListEnd))
         str.append(argListStart, argListEnd - argListStart);
      else if (mType == ConsoleFunctionType && mModule)
      {
         // This isn't correct but the nonsense console stuff is set up such that all
         // functions that have no function bodies are keyed to offset 0 to indicate "no code."
         // This loses the association with the original function definition so we can't really
         // tell here what the actual prototype is except if we searched though the entire opcode
         // stream for the corresponding OP_FUNC_DECL (which would require dealing with the
         // variable-size instructions).

         if (!mFunctionOffset)
            return "()";

         String args = mModule->getFunctionArgs(mFunctionName, mFunctionOffset);
         if (args.isEmpty())
            return "()";

         str.append("( ");
         str.append(args);
         str.append(" )");
      }
   }

   return str.end();
}

String Namespace::Entry::getPrototypeString() const
{
   StringBuilder str;

   // Start with return type.

   if (mHeader && mHeader->mReturnString)
   {
      str.append(sGetDocTypeString(mHeader->mReturnString));
      str.append(' ');
   }
   else
      switch (mType)
      {
         case StringCallbackType:
            str.append("string ");
            break;

         case IntCallbackType:
            str.append("int ");
            break;

         case FloatCallbackType:
            str.append("float ");
            break;

         case VoidCallbackType:
            str.append("void ");
            break;

         case BoolCallbackType:
            str.append("bool ");
            break;

         case ScriptCallbackType:
            break;
      }

   // Add function name and arguments.

   if (mType == ScriptCallbackType)
      str.append(cb.mCallbackName);
   else
      str.append(mFunctionName);

   str.append(getArgumentsString());

   return str.end();
}

String Namespace::Entry::getPrototypeSig() const
{
   StringBuilder str;

   // Add function name and arguments.

   if (mType == ScriptCallbackType)
      str.append(cb.mCallbackName);
   else
      str.append(mFunctionName);
   if (mHeader)
   {
      Vector< String > argList;
      sParseList(mHeader->mArgString, argList);

      const U32 numArgs = argList.size();

      str.append("(%this");

      if (numArgs > 0)
         str.append(',');
      for (U32 i = 0; i < numArgs; ++i)
      {
         // Add separator if not first arg.

         String name;
         String type;

         if (i > 0)
            str.append(',');
         str.append('%');
         sGetArgNameAndType(argList[i], type, name);
         str.append(name);
      }
      str.append(')');
   }

   return str.end();
}
//-----------------------------------------------------------------------------

StringTableEntry Namespace::mActivePackages[Namespace::MaxActivePackages];
U32 Namespace::mNumActivePackages = 0;
U32 Namespace::mOldNumActivePackages = 0;

bool Namespace::isPackage(StringTableEntry name)
{
   for (Namespace *walk = mNamespaceList; walk; walk = walk->mNext)
      if (walk->mPackage == name)
         return true;
   return false;
}

U32 Namespace::getActivePackagesCount()
{
   return mNumActivePackages;
}

StringTableEntry Namespace::getActivePackage(U32 index)
{
   if (index >= mNumActivePackages)
      return StringTable->EmptyString();

   return mActivePackages[index];
}

void Namespace::activatePackage(StringTableEntry name)
{
   if (mNumActivePackages == MaxActivePackages)
   {
      Con::printf("ActivatePackage(%s) failed - Max package limit reached: %d", name, MaxActivePackages);
      return;
   }
   if (!name)
      return;

   // see if this one's already active
   for (U32 i = 0; i < mNumActivePackages; i++)
      if (mActivePackages[i] == name)
         return;

   // kill the cache
   trashCache();

   // find all the package namespaces...
   for (Namespace *walk = mNamespaceList; walk; walk = walk->mNext)
   {
      if (walk->mPackage == name)
      {
         Namespace *parent = Namespace::find(walk->mName);
         // hook the parent
         walk->mParent = parent->mParent;
         parent->mParent = walk;

         // now swap the entries:
         Entry *ew;
         for (ew = parent->mEntryList; ew; ew = ew->mNext)
            ew->mNamespace = walk;

         for (ew = walk->mEntryList; ew; ew = ew->mNext)
            ew->mNamespace = parent;

         ew = walk->mEntryList;
         walk->mEntryList = parent->mEntryList;
         parent->mEntryList = ew;
      }
   }
   mActivePackages[mNumActivePackages++] = name;
}

void Namespace::deactivatePackage(StringTableEntry name)
{
   U32 oldNumActivePackages = mNumActivePackages;

   // Remove all packages down to the given one
   deactivatePackageStack(name);

   // Now add back all packages that followed the given one
   if (!oldNumActivePackages)
      return;
   for (U32 i = mNumActivePackages + 1; i < oldNumActivePackages; i++)
      activatePackage(mActivePackages[i]);
}

void Namespace::deactivatePackageStack(StringTableEntry name)
{
   S32 i, j;
   for (i = 0; i < mNumActivePackages; i++)
      if (mActivePackages[i] == name)
         break;
   if (i == mNumActivePackages)
      return;

   trashCache();

   // Remove all packages down to the given one
   for (j = mNumActivePackages - 1; j >= i; j--)
   {
      // gotta unlink em in reverse order...
      for (Namespace *walk = mNamespaceList; walk; walk = walk->mNext)
      {
         if (walk->mPackage == mActivePackages[j])
         {
            Namespace *parent = Namespace::find(walk->mName);
            // hook the parent
            parent->mParent = walk->mParent;
            walk->mParent = NULL;

            // now swap the entries:
            Entry *ew;
            for (ew = parent->mEntryList; ew; ew = ew->mNext)
               ew->mNamespace = walk;

            for (ew = walk->mEntryList; ew; ew = ew->mNext)
               ew->mNamespace = parent;

            ew = walk->mEntryList;
            walk->mEntryList = parent->mEntryList;
            parent->mEntryList = ew;
         }
      }
   }
   mNumActivePackages = i;
}

void Namespace::unlinkPackages()
{
   mOldNumActivePackages = mNumActivePackages;
   if (!mNumActivePackages)
      return;
   deactivatePackageStack(mActivePackages[0]);
}

void Namespace::relinkPackages()
{
   if (!mOldNumActivePackages)
      return;
   for (U32 i = 0; i < mOldNumActivePackages; i++)
      activatePackage(mActivePackages[i]);
}


DefineEngineFunction(isPackage, bool, (String identifier), ,
   "@brief Returns true if the identifier is the name of a declared package.\n\n"
   "@ingroup Packages\n")
{
   StringTableEntry name = StringTable->insert(identifier.c_str());
   return Namespace::isPackage(name);
}

DefineEngineFunction(activatePackage, void, (String packageName), ,
   "@brief Activates an existing package.\n\n"
   "The activation occurs by updating the namespace linkage of existing functions and methods. "
   "If the package is already activated the function does nothing.\n"
   "@ingroup Packages\n")
{
   StringTableEntry name = StringTable->insert(packageName.c_str());
   Namespace::activatePackage(name);
}

DefineEngineFunction(deactivatePackage, void, (String packageName), ,
   "@brief Deactivates a previously activated package.\n\n"
   "The package is deactivated by removing its namespace linkages to any function or method. "
   "If there are any packages above this one in the stack they are deactivated as well. "
   "If the package is not on the stack this function does nothing.\n"
   "@ingroup Packages\n")
{
   StringTableEntry name = StringTable->insert(packageName.c_str());
   Namespace::deactivatePackage(name);
}

DefineEngineFunction(getPackageList, const char*, (), ,
   "@brief Returns a space delimited list of the active packages in stack order.\n\n"
   "@ingroup Packages\n")
{
   if (Namespace::getActivePackagesCount() == 0)
      return "";

   // Determine size of return buffer
   dsize_t buffersize = 0;
   for (U32 i = 0; i < Namespace::getActivePackagesCount(); ++i)
   {
      buffersize += dStrlen(Namespace::getActivePackage(i)) + 1;
   }

   U32 maxBufferSize = buffersize + 1;
   char* returnBuffer = Con::getReturnBuffer(maxBufferSize);
   U32 returnLen = 0;
   for (U32 i = 0; i < Namespace::getActivePackagesCount(); ++i)
   {
      dSprintf(returnBuffer + returnLen, maxBufferSize - returnLen, "%s ", Namespace::getActivePackage(i));
      returnLen = dStrlen(returnBuffer);
   }

   // Trim off the last extra space
   if (returnLen > 0 && returnBuffer[returnLen - 1] == ' ')
      returnBuffer[returnLen - 1] = '\0';

   return returnBuffer;
}
