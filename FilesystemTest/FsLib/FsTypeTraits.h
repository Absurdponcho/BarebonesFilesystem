#pragma once

// Remove reference
template <typename T>
struct FsRemoveReference
{
    using Type = T;
};

template <typename T>
struct FsRemoveReference<T&>
{
    using Type = T;
};

template <typename T>
struct FsRemoveReference<T&&>
{
    using Type = T;
};

template <typename T>
using FsRemoveReferenceT = typename FsRemoveReference<T>::Type;

// Remove const
template <typename T>
struct FsRemoveConst
{
    using Type = T;
};

template <typename T>
struct FsRemoveConst<const T>
{
    using Type = T;
};

template <typename T>
using FsRemoveConstT = typename FsRemoveConst<T>::Type;

// Remove volatile
template <typename T>
struct FsRemoveVolatile
{
    using Type = T;
};

template <typename T>
struct FsRemoveVolatile<volatile T>
{
    using Type = T;
};

template <typename T>
using FsRemoveVolatileT = typename FsRemoveVolatile<T>::Type;

// Remove const and volatile
template <typename T>
using FsRemoveCVT = FsRemoveConstT<FsRemoveVolatileT<T>>;

// Decay
template <typename T>
struct FsDecay
{
    using Type = FsRemoveCVT<FsRemoveReferenceT<T>>;
};

template <typename T>
using FsDecayT = typename FsDecay<T>::Type;

// Is lvalue reference
template <typename T>
struct FsIsLvalueReference
{
	static constexpr bool value = false;
};

template <typename T>
struct FsIsLvalueReference<T&>
{
	static constexpr bool value = true;
};

template <typename T>
constexpr bool FsIsLvalueReferenceV = FsIsLvalueReference<T>::value;

// Forward
template <typename T>
T&& FsForward(FsRemoveReferenceT<T>& Arg) noexcept
{
    return static_cast<T&&>(Arg);
}

template <typename T>
T&& FsForward(FsRemoveReferenceT<T>&& Arg) noexcept
{
    static_assert(!FsIsLvalueReference<T>::value, "Cannot forward an rvalue as an lvalue.");
    return static_cast<T&&>(Arg);
}

// Move
template <typename T>
FsRemoveReferenceT<T>&& FsMove(T&& Arg) noexcept
{
	return static_cast<FsRemoveReferenceT<T>&&>(Arg);
}