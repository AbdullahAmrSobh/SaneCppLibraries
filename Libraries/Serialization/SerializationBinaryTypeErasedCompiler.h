// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#pragma once
#include "../Reflection/Reflection.h"
#include "../Reflection/ReflectionFlatSchemaCompiler.h"
namespace SC
{
namespace Reflection
{

struct VectorVTable
{
    enum class DropEccessItems
    {
        No,
        Yes
    };

    typedef bool (*FunctionGetSegmentSpan)(MetaProperties property, SpanVoid<void> object, SpanVoid<void>& itemBegin);
    typedef bool (*FunctionGetSegmentSpanConst)(MetaProperties property, SpanVoid<const void> object,
                                                SpanVoid<const void>& itemBegin);

    typedef bool (*FunctionResize)(SpanVoid<void> object, Reflection::MetaProperties property, uint64_t sizeInBytes,
                                   DropEccessItems dropEccessItems);
    typedef bool (*FunctionResizeWithoutInitialize)(SpanVoid<void> object, Reflection::MetaProperties property,
                                                    uint64_t sizeInBytes, DropEccessItems dropEccessItems);
    FunctionGetSegmentSpan          getSegmentSpan;
    FunctionGetSegmentSpanConst     getSegmentSpanConst;
    FunctionResize                  resize;
    FunctionResizeWithoutInitialize resizeWithoutInitialize;
    uint32_t                        linkID;

    constexpr VectorVTable()
        : getSegmentSpan(nullptr), getSegmentSpanConst(nullptr), resize(nullptr), resizeWithoutInitialize(nullptr),
          linkID(0)
    {}
};
template <int MAX_VTABLES>
struct ReflectionVTables
{
    SizedArray<VectorVTable, MAX_VTABLES> vector;
};

struct MetaClassBuilderTypeErased : public MetaClassBuilder<MetaClassBuilderTypeErased>
{
    using Atom = AtomBase<MetaClassBuilderTypeErased>;

    static const uint32_t          MAX_VTABLES = 100;
    ReflectionVTables<MAX_VTABLES> vtables;

    constexpr MetaClassBuilderTypeErased(Atom* output = nullptr, const uint32_t capacity = 0)
        : MetaClassBuilder(output, capacity)
    {}
};

template <typename Container, typename ItemType, int N>
struct VectorArrayVTable<MetaClassBuilderTypeErased, Container, ItemType, N>
{
    constexpr static void build(MetaClassBuilderTypeErased& builder)
    {
        VectorVTable vector;
        vector.resize = &resize;
        assignResizeWithoutInitialize(vector);
        vector.getSegmentSpan      = &getSegmentSpan<void>;
        vector.getSegmentSpanConst = &getSegmentSpan<const void>;
        vector.linkID              = builder.initialSize + static_cast<unsigned>(builder.atoms.size);
        (void)builder.vtables.vector.push_back(vector);
    }

    static bool resize(SpanVoid<void> object, Reflection::MetaProperties property, uint64_t sizeInBytes,
                       VectorVTable::DropEccessItems dropEccessItems)
    {
        SC_UNUSED(property);
        SC_UNUSED(dropEccessItems);
        if (object.sizeInBytes() >= sizeof(void*))
        {
            auto&      vectorByte = *static_cast<Container*>(object.data());
            const auto numItems   = N >= 0 ? min(sizeInBytes / sizeof(ItemType), static_cast<decltype(sizeInBytes)>(N))
                                           : sizeInBytes / sizeof(ItemType);
            return vectorByte.resize(numItems);
        }
        else
        {
            return false;
        }
    }
    static bool resizeWithoutInitialize(SpanVoid<void> object, Reflection::MetaProperties property,
                                        uint64_t sizeInBytes, VectorVTable::DropEccessItems dropEccessItems)
    {
        SC_UNUSED(property);
        SC_UNUSED(dropEccessItems);
        if (object.sizeInBytes() >= sizeof(void*))
        {
            auto&      vectorByte = *static_cast<Container*>(object.data());
            const auto numItems   = N >= 0 ? min(sizeInBytes / sizeof(ItemType), static_cast<decltype(sizeInBytes)>(N))
                                           : sizeInBytes / sizeof(ItemType);
            return vectorByte.resizeWithoutInitializing(numItems);
        }
        else
        {
            return false;
        }
    }

    template <typename VoidType>
    [[nodiscard]] static constexpr bool getSegmentSpan(Reflection::MetaProperties property, SpanVoid<VoidType> object,
                                                       SpanVoid<VoidType>& itemBegin)
    {
        SC_UNUSED(property);
        if (object.sizeInBytes() >= sizeof(void*))
        {
            typedef typename SameConstnessAs<VoidType, Container>::type VectorType;
            auto& vectorByte = *static_cast<VectorType*>(object.data());
            itemBegin        = SpanVoid<VoidType>(vectorByte.data(), vectorByte.size() * sizeof(ItemType));
            return true;
        }
        else
        {
            return false;
        }
    }
    template <typename Q = ItemType>
    [[nodiscard]] static typename EnableIf<not IsTriviallyCopyable<Q>::value, void>::type //
        constexpr assignResizeWithoutInitialize(VectorVTable& vector)
    {
        SC_UNUSED(vector);
    }

    template <typename Q = ItemType>
    [[nodiscard]] static typename EnableIf<IsTriviallyCopyable<Q>::value, void>::type //
        constexpr assignResizeWithoutInitialize(VectorVTable& vector)
    {
        vector.resizeWithoutInitialize = &resizeWithoutInitialize;
    }
};

using FlatSchemaTypeErased = Reflection::FlatSchemaCompiler<MetaClassBuilderTypeErased>;

} // namespace Reflection

namespace SerializationBinaryTypeErased
{

struct ArrayAccess
{
    Span<const Reflection::VectorVTable> vectorVtable;

    [[nodiscard]] bool getSegmentSpan(uint32_t linkID, Reflection::MetaProperties property, SpanVoid<void> object,
                                      SpanVoid<void>& itemBegin);
    [[nodiscard]] bool getSegmentSpan(uint32_t linkID, Reflection::MetaProperties property, SpanVoid<const void> object,
                                      SpanVoid<const void>& itemBegin);

    typedef Reflection::VectorVTable::DropEccessItems DropEccessItems;
    enum class Initialize
    {
        No,
        Yes
    };

    bool resize(uint32_t linkID, SpanVoid<void> object, Reflection::MetaProperties property, uint64_t sizeInBytes,
                Initialize initialize, DropEccessItems dropEccessItems);
};
} // namespace SerializationBinaryTypeErased
} // namespace SC
