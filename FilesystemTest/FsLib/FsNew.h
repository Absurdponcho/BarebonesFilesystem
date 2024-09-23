#pragma once

#ifdef __PLACEMENT_NEW_INLINE
static_assert("FsNew.h must be included before any windows headers");
#endif

#ifdef __PLACEMENT_VEC_NEW_INLINE
static_assert("FsNew.h must be included before any windows headers");
#endif

#define __PLACEMENT_NEW_INLINE
#define __PLACEMENT_VEC_NEW_INLINE
inline void* operator new(size_t, void* p) noexcept { return p; }
inline void* operator new[](size_t, void* p) noexcept { return p; }
inline void  operator delete  (void*, void*) noexcept { };
inline void  operator delete[](void*, void*) noexcept { };