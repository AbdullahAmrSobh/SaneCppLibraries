// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#pragma once
#include "../Foundation/Array.h"
#include "../Foundation/Result.h"
#include "../Foundation/Vector.h"
#include "../Reflection/ReflectionSC.h"
#include "SerializationTemplateCompiler.h"

namespace SC
{
namespace Serialization
{
template <typename BinaryStream>
struct BinarySkipper;
}
} // namespace SC

namespace SC
{
namespace SerializationTemplate
{
template <typename BinaryStream, typename T, typename SFINAESelector = void>
struct Serializer;

template <typename BinaryStream, typename T>
struct SerializerMemberIterator
{
    BinaryStream& stream;

    template <typename R, int N>
    constexpr bool operator()(int order, const char (&name)[N], R& field) const
    {
        SC_UNUSED(order);
        SC_UNUSED(name);
        return Serializer<BinaryStream, R>::serialize(field, stream);
    }
};

struct VersionSchema
{
    struct Options
    {
        bool allowFloatToIntTruncation    = true;
        bool allowDropEccessArrayItems    = true;
        bool allowDropEccessStructMembers = true;
    };
    Options options;

    Span<const Reflection::MetaProperties> sourceProperties;

    uint32_t sourceTypeIndex = 0;

    constexpr Reflection::MetaProperties current() const { return sourceProperties.data()[sourceTypeIndex]; }

    constexpr void advance() { sourceTypeIndex++; }

    constexpr void resolveLink()
    {
        if (sourceProperties.data()[sourceTypeIndex].getLinkIndex() >= 0)
            sourceTypeIndex = static_cast<uint32_t>(sourceProperties.data()[sourceTypeIndex].getLinkIndex());
    }

    template <typename BinaryStream>
    [[nodiscard]] constexpr bool skipCurrent(BinaryStream& stream)
    {
        Serialization::BinarySkipper<BinaryStream> skipper(stream, sourceTypeIndex);
        skipper.sourceProperties = sourceProperties;
        return skipper.skip();
    }
};

template <typename BinaryStream, typename T>
struct SerializerVersionedMemberIterator
{
    VersionSchema& schema;
    BinaryStream&  stream;
    int            matchOrder          = 0;
    bool           consumed            = false;
    bool           consumedWithSuccess = false;

    template <typename R, int N>
    constexpr bool operator()(int order, const char (&name)[N], R& field)
    {
        SC_UNUSED(name);
        if (matchOrder == order)
        {
            consumed            = true;
            consumedWithSuccess = Serializer<BinaryStream, R>::serializeVersioned(field, stream, schema);
            return false; // stop iterations
        }
        return true;
    }
};

template <typename BinaryStream, typename T, typename SFINAESelector>
struct Serializer
{
    static constexpr bool IsItemPacked = Reflection::MetaTypeInfo<T>::IsPacked;

    [[nodiscard]] static constexpr bool serializeVersioned(T& object, BinaryStream& stream, VersionSchema& schema)
    {
        typedef SerializerVersionedMemberIterator<BinaryStream, T> VersionedMemberIterator;
        if (schema.current().type != Reflection::MetaType::TypeStruct)
        {
            return false;
        }
        const uint32_t numMembers      = static_cast<uint32_t>(schema.current().numSubAtoms);
        const auto     structTypeIndex = schema.sourceTypeIndex;
        for (uint32_t idx = 0; idx < numMembers; ++idx)
        {
            schema.sourceTypeIndex          = structTypeIndex + idx + 1;
            VersionedMemberIterator visitor = {schema, stream, schema.current().order};
            schema.resolveLink();
            Reflection::MetaClass<T>::visitObject(visitor, object);
            if (visitor.consumed)
            {
                SC_TRY_IF(visitor.consumedWithSuccess);
            }
            else
            {
                SC_TRY_IF(schema.options.allowDropEccessStructMembers)
                // We must consume it anyway, discarding its content
                SC_TRY_IF(schema.skipCurrent(stream));
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr bool serialize(T& object, BinaryStream& stream)
    {
        if (IsItemPacked)
        {
            return stream.serialize({&object, sizeof(T)});
        }
        return Reflection::MetaClass<T>::visitObject(SerializerMemberIterator<BinaryStream, T>{stream}, object);
    }
};

template <typename BinaryStream, typename T>
struct SerializerItems
{
    [[nodiscard]] static constexpr bool serializeItems(T* object, BinaryStream& stream, VersionSchema& schema,
                                                       uint32_t numSourceItems, uint32_t numDestinationItems)
    {
        schema.resolveLink();
        const auto commonSubsetItems  = min(numSourceItems, numDestinationItems);
        const auto arrayItemTypeIndex = schema.sourceTypeIndex;

        const bool isMemcpyable =
            Reflection::IsPrimitive<T>::value && schema.current().type == Reflection::MetaClass<T>::getMetaType();
        if (isMemcpyable)
        {
            const size_t sourceNumBytes = schema.current().sizeInBytes * numSourceItems;
            const size_t destNumBytes   = numDestinationItems * sizeof(T);
            const size_t minBytes       = min(destNumBytes, sourceNumBytes);
            SC_TRY_IF(stream.serialize({object, minBytes}));
            if (sourceNumBytes > destNumBytes)
            {
                // We must consume these excess bytes anyway, discarding their content
                SC_TRY_IF(schema.options.allowDropEccessArrayItems);
                return stream.advance(sourceNumBytes - minBytes);
            }
            return true;
        }

        for (uint32_t idx = 0; idx < commonSubsetItems; ++idx)
        {
            schema.sourceTypeIndex = arrayItemTypeIndex;
            if (not Serializer<BinaryStream, T>::serializeVersioned(object[idx], stream, schema))
                return false;
        }

        if (numSourceItems > numDestinationItems)
        {
            // We must consume these excess items anyway, discarding their content
            SC_TRY_IF(schema.options.allowDropEccessArrayItems);

            for (uint32_t idx = 0; idx < numSourceItems - numDestinationItems; ++idx)
            {
                schema.sourceTypeIndex = arrayItemTypeIndex;
                SC_TRY_IF(schema.skipCurrent(stream));
            }
        }
        return true;
    }
};

template <typename BinaryStream, typename T, int N>
struct Serializer<BinaryStream, T[N]>
{
    static constexpr bool IsItemPacked = Reflection::MetaTypeInfo<T>::IsPacked;

    [[nodiscard]] static constexpr bool serializeVersioned(T (&object)[N], BinaryStream& stream, VersionSchema& schema)
    {
        schema.advance(); // make T current type
        return SerializerItems<BinaryStream, T>::serializeVersioned(
            object, stream, schema, schema.current().getCustomUint32(), static_cast<uint32_t>(N));
    }

    [[nodiscard]] static constexpr bool serialize(T (&object)[N], BinaryStream& stream)
    {
        if (IsItemPacked)
        {
            return stream.serialize({object, sizeof(object)});
        }
        else
        {
            for (auto& item : object)
            {
                if (not Serializer<BinaryStream, T>::serialize(item, stream))
                    return false;
            }
            return true;
        }
    }
};

template <typename BinaryStream, typename Container, typename T>
struct SerializerVector
{
    static constexpr bool IsItemPacked = Reflection::MetaTypeInfo<T>::IsPacked;

    [[nodiscard]] static constexpr bool serialize(Container& object, BinaryStream& stream)
    {
        const size_t itemSize    = sizeof(T);
        uint64_t     sizeInBytes = static_cast<uint64_t>(object.size() * itemSize);
        if (not Serializer<BinaryStream, uint64_t>::serialize(sizeInBytes, stream))
            return false;
        SC_TRY_IF(object.resize(sizeInBytes / itemSize));

        if (IsItemPacked)
        {
            return stream.serialize({object.data(), itemSize * object.size()});
        }
        else
        {
            for (auto& item : object)
            {
                if (!Serializer<BinaryStream, T>::serialize(item, stream))
                    return false;
            }
            return true;
        }
    }
};

template <typename BinaryStream, typename T>
struct Serializer<BinaryStream, SC::Vector<T>> : public SerializerVector<BinaryStream, SC::Vector<T>, T>
{
    [[nodiscard]] static constexpr bool serializeVersioned(SC::Vector<T>& object, BinaryStream& stream,
                                                           VersionSchema& schema)
    {
        uint64_t sizeInBytes = 0;
        if (not Serializer<BinaryStream, uint64_t>::serialize(sizeInBytes, stream))
            return false;
        schema.advance();
        const bool isMemcpyable =
            Reflection::IsPrimitive<T>::value && schema.current().type == Reflection::MetaClass<T>::getMetaType();
        const size_t   sourceItemSize = schema.current().sizeInBytes;
        const uint32_t numSourceItems = static_cast<uint32_t>(sizeInBytes / sourceItemSize);
        if (isMemcpyable)
        {
            SC_TRY_IF(object.resizeWithoutInitializing(numSourceItems));
        }
        else
        {
            SC_TRY_IF(object.resize(numSourceItems));
        }
        return SerializerItems<BinaryStream, T>::serializeVersioned(object.data(), stream, schema, numSourceItems,
                                                                    numSourceItems);
    }
};

template <typename BinaryStream, typename T, int N>
struct Serializer<BinaryStream, SC::Array<T, N>> : public SerializerVector<BinaryStream, SC::Array<T, N>, T>
{
    [[nodiscard]] static constexpr bool serializeVersioned(SC::Array<T, N>& object, BinaryStream& stream,
                                                           VersionSchema& schema)
    {
        uint64_t sizeInBytes = 0;
        if (not Serializer<BinaryStream, uint64_t>::serialize(sizeInBytes, stream))
            return false;
        schema.advance();
        const bool isMemcpyable =
            Reflection::IsPrimitive<T>::value && schema.current().type == Reflection::MetaClass<T>::getMetaType();

        const size_t   sourceItemSize      = schema.current().sizeInBytes;
        const uint32_t numSourceItems      = static_cast<uint32_t>(sizeInBytes / sourceItemSize);
        const uint32_t numDestinationItems = static_cast<uint32_t>(N);
        if (isMemcpyable)
        {
            SC_TRY_IF(object.resizeWithoutInitializing(min(numSourceItems, numDestinationItems)));
        }
        else
        {
            SC_TRY_IF(object.resize(min(numSourceItems, numDestinationItems)));
        }
        return SerializerItems<BinaryStream, T>::serializeItems(object.data(), stream, schema, numSourceItems,
                                                                numDestinationItems);
    }
};

template <typename BinaryStream, typename T>
struct Serializer<BinaryStream, T, typename SC::EnableIf<Reflection::IsPrimitive<T>::value>::type>
{
    template <typename ValueType>
    [[nodiscard]] static bool readCastValue(T& destination, BinaryStream& stream)
    {
        ValueType value;
        SC_TRY_IF(stream.serialize({&value, sizeof(ValueType)}));
        destination = static_cast<T>(value);
        return true;
    }

    [[nodiscard]] static constexpr bool serializeVersioned(T& object, BinaryStream& stream, VersionSchema& schema)
    {
        // clang-format off
        switch (schema.current().type)
        {
            case Reflection::MetaType::TypeUINT8:      return readCastValue<uint8_t>(object, stream);
            case Reflection::MetaType::TypeUINT16:     return readCastValue<uint16_t>(object, stream);
            case Reflection::MetaType::TypeUINT32:     return readCastValue<uint32_t>(object, stream);
            case Reflection::MetaType::TypeUINT64:     return readCastValue<uint64_t>(object, stream);
            case Reflection::MetaType::TypeINT8:       return readCastValue<int8_t>(object, stream);
            case Reflection::MetaType::TypeINT16:      return readCastValue<int16_t>(object, stream);
            case Reflection::MetaType::TypeINT32:      return readCastValue<int32_t>(object, stream);
            case Reflection::MetaType::TypeINT64:      return readCastValue<int64_t>(object, stream);
            case Reflection::MetaType::TypeFLOAT32:
            {
                if(schema.options.allowFloatToIntTruncation || IsSame<T, float>::value || IsSame<T, double>::value)
                {
                    return readCastValue<float>(object, stream);
                }
                return false;
            }
            case Reflection::MetaType::TypeDOUBLE64:
            {
                if(schema.options.allowFloatToIntTruncation || IsSame<T, float>::value || IsSame<T, double>::value)
                {
                    return readCastValue<double>(object, stream);
                }
                return false;
            }
            default: return false;
        }
        // clang-format on
    }

    [[nodiscard]] static constexpr bool serialize(T& object, BinaryStream& stream)
    {
        return stream.serialize({&object, sizeof(T)});
    }
};
} // namespace SerializationTemplate
} // namespace SC
