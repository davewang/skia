/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPixelRef_DEFINED
#define SkPixelRef_DEFINED

#include "../private/SkAtomics.h"
#include "../private/SkMutex.h"
#include "../private/SkTDArray.h"
#include "SkBitmap.h"
#include "SkFilterQuality.h"
#include "SkImageInfo.h"
#include "SkPixmap.h"
#include "SkRefCnt.h"
#include "SkSize.h"
#include "SkString.h"

class SkColorTable;
struct SkIRect;

class GrTexture;
class SkDiscardableMemory;

/** \class SkPixelRef

    This class is the smart container for pixel memory, and is used with SkBitmap.
    This class can be shared/accessed between multiple threads.
*/
class SK_API SkPixelRef : public SkRefCnt {
public:
    explicit SkPixelRef(const SkImageInfo&, void* addr, size_t rowBytes,
                        sk_sp<SkColorTable> = nullptr);
    virtual ~SkPixelRef();

    const SkImageInfo& info() const {
        return fInfo;
    }

    void* pixels() const { return fRec.fPixels; }
    SkColorTable* colorTable() const { return fRec.fColorTable; }
    size_t rowBytes() const { return fRec.fRowBytes; }

    /**
     *  To access the actual pixels of a pixelref, it must be "locked".
     *  Calling lockPixels returns a LockRec struct (on success).
     */
    struct LockRec {
        LockRec() : fPixels(NULL), fColorTable(NULL) {}

        void*           fPixels;
        SkColorTable*   fColorTable;
        size_t          fRowBytes;

        void zero() { sk_bzero(this, sizeof(*this)); }

        bool isZero() const {
            return NULL == fPixels && NULL == fColorTable && 0 == fRowBytes;
        }
    };

    bool lockPixels() { return true; }
    void unlockPixels() {}

    /**
     *  Call to access the pixel memory. On success, return true and fill out
     *  the specified rec. On failure, return false and ignore the rec parameter.
     *  Balance this with a call to unlockPixels().
     */
    bool lockPixels(LockRec* rec);


    /** Returns a non-zero, unique value corresponding to the pixels in this
        pixelref. Each time the pixels are changed (and notifyPixelsChanged is
        called), a different generation ID will be returned.
    */
    uint32_t getGenerationID() const;

#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK
    /** Returns a non-zero, unique value corresponding to this SkPixelRef.
        Unlike the generation ID, this ID remains the same even when the pixels
        are changed. IDs are not reused (until uint32_t wraps), so it is safe
        to consider this ID unique even after this SkPixelRef is deleted.

        Can be used as a key which uniquely identifies this SkPixelRef
        regardless of changes to its pixels or deletion of this object.
     */
    uint32_t getStableID() const { return fStableID; }
#endif

    /**
     *  Call this if you have changed the contents of the pixels. This will in-
     *  turn cause a different generation ID value to be returned from
     *  getGenerationID().
     */
    void notifyPixelsChanged();

    /**
     *  Change the info's AlphaType. Note that this does not automatically
     *  invalidate the generation ID. If the pixel values themselves have
     *  changed, then you must explicitly call notifyPixelsChanged() as well.
     */
    void changeAlphaType(SkAlphaType at);

    /** Returns true if this pixelref is marked as immutable, meaning that the
        contents of its pixels will not change for the lifetime of the pixelref.
    */
    bool isImmutable() const { return fMutability != kMutable; }

    /** Marks this pixelref is immutable, meaning that the contents of its
        pixels will not change for the lifetime of the pixelref. This state can
        be set on a pixelref, but it cannot be cleared once it is set.
    */
    void setImmutable();

    struct LockRequest {
        SkISize         fSize;
        SkFilterQuality fQuality;
    };

    struct LockResult {
        LockResult() : fPixels(NULL), fCTable(NULL) {}

        void        (*fUnlockProc)(void* ctx);
        void*       fUnlockContext;

        const void* fPixels;
        SkColorTable* fCTable;  // should be NULL unless colortype is kIndex8
        size_t      fRowBytes;
        SkISize     fSize;

        void unlock() {
            if (fUnlockProc) {
                fUnlockProc(fUnlockContext);
                fUnlockProc = NULL; // can't unlock twice!
            }
        }
    };

    bool requestLock(const LockRequest&, LockResult*);

    // Register a listener that may be called the next time our generation ID changes.
    //
    // We'll only call the listener if we're confident that we are the only SkPixelRef with this
    // generation ID.  If our generation ID changes and we decide not to call the listener, we'll
    // never call it: you must add a new listener for each generation ID change.  We also won't call
    // the listener when we're certain no one knows what our generation ID is.
    //
    // This can be used to invalidate caches keyed by SkPixelRef generation ID.
    struct GenIDChangeListener {
        virtual ~GenIDChangeListener() {}
        virtual void onChange() = 0;
    };

    // Takes ownership of listener.
    void addGenIDChangeListener(GenIDChangeListener* listener);

    // Call when this pixelref is part of the key to a resourcecache entry. This allows the cache
    // to know automatically those entries can be purged when this pixelref is changed or deleted.
    void notifyAddedToCache() {
        fAddedToCache.store(true);
    }

    virtual SkDiscardableMemory* diagnostic_only_getDiscardable() const { return NULL; }

protected:
    // default impl does nothing.
    virtual void onNotifyPixelsChanged();

    /**
     *  Returns the size (in bytes) of the internally allocated memory.
     *  This should be implemented in all serializable SkPixelRef derived classes.
     *  SkBitmap::fPixelRefOffset + SkBitmap::getSafeSize() should never overflow this value,
     *  otherwise the rendering code may attempt to read memory out of bounds.
     *
     *  @return default impl returns 0.
     */
    virtual size_t getAllocatedSizeInBytes() const;

#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK
    // This is undefined if there are clients in-flight trying to use us
    void android_only_reset(const SkImageInfo&, size_t rowBytes, sk_sp<SkColorTable>);
#endif

private:
    // mostly const. fInfo.fAlpahType can be changed at runtime.
    const SkImageInfo fInfo;
    sk_sp<SkColorTable> fCTable;    // duplicated in LockRec, will unify later

    // LockRec is only valid if we're in a locked state (isLocked())
    LockRec         fRec;

    // Bottom bit indicates the Gen ID is unique.
    bool genIDIsUnique() const { return SkToBool(fTaggedGenID.load() & 1); }
    mutable SkAtomic<uint32_t> fTaggedGenID;

#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK
    const uint32_t fStableID;
#endif

    SkTDArray<GenIDChangeListener*> fGenIDChangeListeners;  // pointers are owned

    // Set true by caches when they cache content that's derived from the current pixels.
    SkAtomic<bool> fAddedToCache;

    enum {
        kMutable,               // PixelRefs begin mutable.
        kTemporarilyImmutable,  // Considered immutable, but can revert to mutable.
        kImmutable,             // Once set to this state, it never leaves.
    } fMutability : 8;          // easily fits inside a byte

    // only ever set in constructor, const after that
    bool fPreLocked;

    void needsNewGenID();
    void callGenIDChangeListeners();

    void setTemporarilyImmutable();
    void restoreMutability();
    friend class SkSurface_Raster;   // For the two methods above.

    bool isPreLocked() const { return fPreLocked; }
    friend class SkImage_Raster;
    friend class SkSpecialImage_Raster;

    // When copying a bitmap to another with the same shape and config, we can safely
    // clone the pixelref generation ID too, which makes them equivalent under caching.
    friend class SkBitmap;  // only for cloneGenID
    void cloneGenID(const SkPixelRef&);

    void setImmutableWithID(uint32_t genID);
    friend class SkImage_Gpu;
    friend class SkImageCacherator;
    friend class SkSpecialImage_Gpu;
    friend void SkBitmapCache_setImmutableWithID(SkPixelRef*, uint32_t);

    typedef SkRefCnt INHERITED;
};

#endif
