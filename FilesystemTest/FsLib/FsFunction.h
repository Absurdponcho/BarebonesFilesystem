#pragma once
#include "FsTypeTraits.h"

class FsFunctionBase
{
public:
    virtual ~FsFunctionBase() = default;
    virtual void Invoke() = 0;
    virtual FsFunctionBase* Clone() const = 0;
};

template <typename TCallable>
class FsFunctionHolder : public FsFunctionBase
{
public:
    FsFunctionHolder(TCallable&& TCallable) : Callable(FsForward<TCallable>(TCallable)) {}
    FsFunctionHolder(const TCallable& TCallable) : Callable(TCallable) {}

    void Invoke() override
    {
        Callable();
    }

    FsFunctionBase* Clone() const override
    {
        return new FsFunctionHolder(Callable);
    }

private:
    TCallable Callable;
};

class FsFunction
{
public:
    FsFunction() : Callable(nullptr) {}

    template <typename TCallable>
    FsFunction(TCallable&& Callable)
        : Callable(new FsFunctionHolder<FsDecayT<TCallable>>(FsForward<TCallable>(Callable)))
    {
    }

    FsFunction(const FsFunction& Other)
        : Callable(Other.Callable ? Other.Callable->Clone() : nullptr)
    {
    }

    FsFunction(FsFunction&& Other) noexcept
        : Callable(Other.Callable)
    {
        Other.Callable = nullptr;
    }

    FsFunction& operator=(const FsFunction& Other)
    {
        if (this != &Other)
        {
            delete Callable;
            Callable = Other.Callable ? Other.Callable->Clone() : nullptr;
        }
        return *this;
    }

    FsFunction& operator=(FsFunction&& Other) noexcept
    {
        if (this != &Other)
        {
            delete Callable;
            Callable = Other.Callable;
            Other.Callable = nullptr;
        }
        return *this;
    }

    ~FsFunction()
    {
        delete Callable;
    }

    void operator()() const
    {
        if (Callable)
        {
            Callable->Invoke();
        }
    }

    explicit operator bool() const
    {
        return Callable != nullptr;
    }

private:
    FsFunctionBase* Callable;
};
