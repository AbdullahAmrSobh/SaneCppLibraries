// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#pragma once
#include "Internal/Segment.h"

namespace SC
{
struct ArrayAllocator;
template <typename T, int N>
struct Array;
} // namespace SC

//! @addtogroup group_containers
//! @{
struct SC::ArrayAllocator
{
    [[nodiscard]] static SegmentHeader* reallocate(SegmentHeader* oldHeader, size_t newSize);
    [[nodiscard]] static SegmentHeader* allocate(SegmentHeader* oldHeader, size_t numNewBytes, void* pself);
    static void                         release(SegmentHeader* oldHeader);

    template <typename T>
    static T* getItems(SegmentHeader* header)
    {
        return reinterpret_cast<T*>(reinterpret_cast<char*>(header) + sizeof(SegmentHeader));
    }
    template <typename T>
    static const T* getItems(const SegmentHeader* header)
    {
        return reinterpret_cast<const T*>(reinterpret_cast<const char*>(header) + sizeof(SegmentHeader));
    }
};

/// @brief A contiguous sequence of elements kept inside its inline storage
/// @tparam T Type of single element of the Array
/// @tparam N Number of elements contained inside this Array inline storage
template <typename T, int N>
struct SC::Array
{
  protected:
    static_assert(N > 0, "Array must have N > 0");

    SegmentItems<T> segmentHeader;
    union
    {
        T items[N];
    };

    using Parent     = SegmentItems<T>;
    using Operations = SegmentOperations<ArrayAllocator, T>;
    template <int>
    friend struct SmallString;
    template <typename, int>
    friend struct SmallVector;

  public:
    /// @brief Constructs an empty Array
    Array();

    /// @brief Constructs a Array from an initializer list
    /// @param ilist The initializer list that will be appended to this Array
    Array(std::initializer_list<T> ilist);

    /// @brief Destroys the Array, releasing allocated memory
    ~Array() { Operations::destroy(&segmentHeader); }

    /// @brief Copies array into this array
    /// @param other The array to be copied
    Array(const Array& other);

    /// @brief Moves array contents into this array
    /// @param other The array being moved
    Array(Array&& other);

    /// @brief Move assigns another array to this one. Contents of this array will be freed.
    /// @param other The array being move assigned
    /// @return Reference to this array
    Array& operator=(const Array& other);

    /// @brief Copy assigns another array to this one. Contents of this array will be freed.
    /// @param other The array being copy assigned
    /// @return Reference to this array
    Array& operator=(Array&& other);

    template <int M>
    Array(const Array<T, M>& other);

    template <int M>
    Array(Array<T, M>&& other);

    template <int M>
    Array& operator=(const Array<T, M>& other);

    template <int M>
    Array& operator=(Array<T, M>&& other);

    /// @brief Returns a Span wrapping the entire of current array
    /// @return a Span wrapping the entire of current array
    [[nodiscard]] Span<const T> toSpanConst() const { return {items, size()}; }

    /// @brief Returns a Span wrapping the entire of current array
    /// @return a Span wrapping the entire of current array
    [[nodiscard]] Span<T> toSpan() { return {items, size()}; }

    /// @brief Access item at index. Bounds checked in debug.
    /// @param index index of the item to be accessed
    /// @return Value at index in the array
    [[nodiscard]] T& operator[](size_t index);

    /// @brief Access item at index. Bounds checked in debug.
    /// @param index index of the item to be accessed
    /// @return Value at index in the array
    [[nodiscard]] const T& operator[](size_t index) const;

    /// @brief Copies an element in front of the Array, at position 0
    /// @param element The element to be copied in front of the Array
    /// @return `true` if operation succeded
    [[nodiscard]] bool push_front(const T& element) { return insert(0, {&element, 1}); }

    /// @brief Moves an element in front of the Array, at position 0
    /// @param element The element to be moved in front of the Array
    /// @return `true` if operation succeded
    [[nodiscard]] bool push_front(T&& element) { return insertMove(0, {&element, 1}); }

    /// @brief Appends an element copying it at the end of the Array
    /// @param element The element to be copied at the end of the Array
    /// @return `true` if operation succeded
    [[nodiscard]] bool push_back(const T& element);

    /// @brief Appends an element moving it at the end of the Array
    /// @param element The element to be moved at the end of the Array
    /// @return `true` if operation succeded
    [[nodiscard]] bool push_back(T&& element);

    /// @brief Removes the last element of the array
    /// @return `true` if the operation succeeds
    [[nodiscard]] bool pop_back();

    /// @brief Removes the first element of the array
    /// @return `true` if the operation succeeds
    [[nodiscard]] bool pop_front();

    /// @brief Access the first element of the Array
    /// @return A reference to the first element of the Array
    [[nodiscard]] T& front();

    /// @brief Access the first element of the Array
    /// @return A reference to the first element of the Array
    [[nodiscard]] const T& front() const;

    /// @brief Access the last element of the Array
    /// @return A reference to the last element of the Array
    [[nodiscard]] T& back();

    /// @brief Access the last element of the Array
    /// @return A reference to the last element of the Array
    [[nodiscard]] const T& back() const;

    /// @brief Reserves memory for newCapacity elements, allocating memory if necessary.
    /// @param newCapacity The wanted new capacity for this Array
    /// @return `true` if memory reservation succeded
    [[nodiscard]] bool reserve(size_t newCapacity) { return newCapacity <= capacity(); }

    /// @brief Resizes this array to newSize, preserving existing elements.
    /// @param newSize The wanted new size of the array
    /// @param value a default value that will be used for new elements inserted.
    /// @return `true` if resize succeded
    [[nodiscard]] bool resize(size_t newSize, const T& value = T());

    /// @brief Resizes this array to newSize, preserving existing elements. Does not initialize the items between
    /// size() and capacity().
    ///         Be careful, it's up to the caller to initialize such items to avoid UB.
    /// @param newSize The wanted new size of the array
    /// @return `true` if resize succeded
    [[nodiscard]] bool resizeWithoutInitializing(size_t newSize);

    /// @brief  Removes all elements from container, calling destructor for each of them.
    ///         Doesn't deallocate memory (use shrink_to_fit for that)
    void clear() { Operations::clear(SegmentItems<T>::getSegment(items)); }

    /// @brief Sets size() to zero, without calling destructor on elements.
    void clearWithoutInitializing() { (void)resizeWithoutInitializing(0); }

    /// @brief This operation is a no-op on Array.
    /// @return `true`
    [[nodiscard]] bool shrink_to_fit() { return true; }

    /// @brief Check if the array is empty
    /// @return `true` if array is empty.
    [[nodiscard]] bool isEmpty() const { return size() == 0; }

    /// @brief Gets size of the array
    /// @return size of the array
    [[nodiscard]] size_t size() const { return segmentHeader.size(); }

    /// @brief Gets capacity of the array. Capacity is always >= size.
    /// @return capacity of the array
    [[nodiscard]] size_t capacity() const { return segmentHeader.capacity(); }

    /// @brief Gets pointer to first element of the array
    /// @return pointer to first element of the array
    [[nodiscard]] T* begin() { return items; }
    /// @brief Gets pointer to first element of the array
    /// @return pointer to first element of the array
    [[nodiscard]] const T* begin() const { return items; }
    /// @brief Gets pointer to one after last element of the array
    /// @return pointer to one after last element of the array
    [[nodiscard]] T* end() { return items + size(); }
    /// @brief Gets pointer to one after last element of the array
    /// @return pointer to one after last element of the array
    [[nodiscard]] const T* end() const { return items + size(); }
    /// @brief Gets pointer to first element of the array
    /// @return pointer to first element of the array
    [[nodiscard]] T* data() { return items; }
    /// @brief Gets pointer to first element of the array
    /// @return pointer to first element of the array
    [[nodiscard]] const T* data() const { return items; }

    /// @brief Inserts a range of items copying them at given index
    /// @param idx Index where to start inserting the range of items
    /// @param data the range of items to copy
    /// @return `true` if operation succeded
    [[nodiscard]] bool insert(size_t idx, Span<const T> data);

    /// @brief Appends a range of items copying them at the end of array
    /// @param data the range of items to copy
    /// @return `true` if operation succeded
    [[nodiscard]] bool append(Span<const T> data);

    /// @brief Appends a range of items copying them at the end of array
    /// @param data the range of items to copy
    /// @return `true` if operation succeded
    template <typename U>
    [[nodiscard]] bool append(Span<const U> data);

    /// @brief Appends another array moving its contents at the end of array
    /// @tparam U Type of the array to be move appended
    /// @param src The array to be moved at end of array
    /// @return `true` if operation succeded
    template <typename U>
    [[nodiscard]] bool appendMove(U&& src);

    /// @brief Check if the current array contains a given value.
    /// @tparam U Type of the object being searched
    /// @param value Value being searched
    /// @param foundIndex if passed in != `nullptr`, receives index where item was found.
    /// Only written if function returns `true`
    /// @return `true` if the array contains the given value.
    template <typename U>
    [[nodiscard]] bool contains(const U& value, size_t* foundIndex = nullptr) const;

    /// @brief Finds the first item in array matching criteria given by the lambda
    /// @tparam Lambda Type of the Lambda passed that declares a `bool operator()(const T&)` operator
    /// @param lambda The functor or lambda called that evaluates to `true` when item is found
    /// @param foundIndex if passed in != `nullptr`, receives index where item was found.
    /// @return `true` if the wanted value with given criteria is found.
    template <typename Lambda>
    [[nodiscard]] bool find(Lambda&& lambda, size_t* foundIndex = nullptr) const;

    /// @brief Removes an item at a given index
    /// @param index Index where the item must be removed
    /// @return `true` if operation succeeded (index is within bounds)
    [[nodiscard]] bool removeAt(size_t index);

    /// @brief Removes all items matching criteria given by Lambda
    /// @tparam Lambda Type of the functor/lambda with a `bool operator()(const T&)` operator
    /// @param criteria The lambda/functor passed in
    /// @return `true` if at least one item has been removed
    template <typename Lambda>
    [[nodiscard]] bool removeAll(Lambda&& criteria);

    /// @brief Removes all values equal to `value`
    /// @tparam U Type of the Value
    /// @param value Value to be removed
    /// @return `true` if at least one item has been removed
    template <typename U>
    [[nodiscard]] bool remove(const U& value);

  private:
    [[nodiscard]] bool insertMove(size_t idx, T* src, size_t srcSize);

    [[nodiscard]] bool appendMove(Span<T> data);
};
//! @}

//-----------------------------------------------------------------------------------------------------------------------
// Implementation details
//-----------------------------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------------------------------
// ArrayAllocator
//-----------------------------------------------------------------------------------------------------------------------

inline SC::SegmentHeader* SC::ArrayAllocator::reallocate(SegmentHeader* oldHeader, size_t newSize)
{
    if (newSize <= oldHeader->sizeBytes)
    {
        return oldHeader;
    }
    return nullptr;
}
inline SC::SegmentHeader* SC::ArrayAllocator::allocate(SegmentHeader* oldHeader, size_t numNewBytes, void* pself)
{
    SC_COMPILER_UNUSED(numNewBytes);
    SC_COMPILER_UNUSED(pself);
    oldHeader->initDefaults();
    return oldHeader;
}

inline void SC::ArrayAllocator::release(SegmentHeader* oldHeader) { SC_COMPILER_UNUSED(oldHeader); }

//-----------------------------------------------------------------------------------------------------------------------
// Array<T, N>
//-----------------------------------------------------------------------------------------------------------------------

template <typename T, int N>
SC::Array<T, N>::Array()
{
    static_assert(alignof(Array) == alignof(uint64_t), "Array Alignment");
    segmentHeader.sizeBytes     = 0;
    segmentHeader.capacityBytes = sizeof(T) * N;
}

template <typename T, int N>
SC::Array<T, N>::Array(std::initializer_list<T> ilist)
{
    segmentHeader.capacityBytes = sizeof(T) * N;
    const auto sz               = min(static_cast<int>(ilist.size()), N);
    Parent::copyConstructMultiple(items, 0, static_cast<size_t>(sz), ilist.begin());
    segmentHeader.setSize(static_cast<size_t>(sz));
}

template <typename T, int N>
T& SC::Array<T, N>::front()
{
    const size_t numElements = size();
    SC_ASSERT_RELEASE(numElements > 0);
    return items[0];
}

template <typename T, int N>
const T& SC::Array<T, N>::front() const
{
    const size_t numElements = size();
    SC_ASSERT_RELEASE(numElements > 0);
    return items[0];
}

template <typename T, int N>
T& SC::Array<T, N>::back()
{
    const size_t numElements = size();
    SC_ASSERT_RELEASE(numElements > 0);
    return items[numElements - 1];
}

template <typename T, int N>
const T& SC::Array<T, N>::back() const
{
    const size_t numElements = size();
    SC_ASSERT_RELEASE(numElements > 0);
    return items[numElements - 1];
}
template <typename T, int N>
SC::Array<T, N>::Array(const Array& other)
{
    segmentHeader.sizeBytes     = 0;
    segmentHeader.capacityBytes = sizeof(T) * N;
    (void)append(other.items, other.size());
}

template <typename T, int N>
SC::Array<T, N>::Array(Array&& other)
{
    segmentHeader.sizeBytes     = 0;
    segmentHeader.capacityBytes = sizeof(T) * N;
    (void)appendMove(other.toSpan());
}

template <typename T, int N>
template <int M>
SC::Array<T, N>::Array(const Array<T, M>& other)
{
    static_assert(M <= N, "Unsafe operation, cannot report failure inside constructor, use append instead");
    segmentHeader.sizeBytes     = 0;
    segmentHeader.capacityBytes = sizeof(T) * N;
    (void)append(other.toSpanConst());
}

template <typename T, int N>
template <int M>
SC::Array<T, N>::Array(Array<T, M>&& other)
{
    static_assert(M <= N, "Unsafe operation, cannot report failure inside constructor, use appendMove instead");
    segmentHeader.sizeBytes     = 0;
    segmentHeader.capacityBytes = sizeof(T) * N;
    (void)appendMove(other.items, other.size());
}

template <typename T, int N>
T& SC::Array<T, N>::operator[](size_t index)
{
    SC_ASSERT_DEBUG(index < size());
    return items[index];
}

template <typename T, int N>
const T& SC::Array<T, N>::operator[](size_t index) const
{
    SC_ASSERT_DEBUG(index < size());
    return items[index];
}

template <typename T, int N>
SC::Array<T, N>& SC::Array<T, N>::operator=(const Array& other)
{
    if (&other != this)
    {
        T*   oldItems = Array::items;
        bool res      = Operations::assign(oldItems, other.items, other.size());
        (void)res;
        SC_ASSERT_DEBUG(res);
    }
    return *this;
}

template <typename T, int N>
SC::Array<T, N>& SC::Array<T, N>::operator=(Array&& other)
{
    if (&other != this)
    {
        Operations::clear(SegmentItems<T>::getSegment(items));
        if (appendMove(other.toSpan()))
        {
            Operations::clear(SegmentItems<T>::getSegment(other.items));
        }
    }
    return *this;
}

template <typename T, int N>
template <int M>
SC::Array<T, N>& SC::Array<T, N>::operator=(const Array<T, M>& other)
{
    if (&other != this)
    {
        T*   oldItems = items;
        bool res      = copy(oldItems, other.data(), other.size());
        (void)res;
        SC_ASSERT_DEBUG(res);
    }
    return *this;
}

template <typename T, int N>
template <int M>
SC::Array<T, N>& SC::Array<T, N>::operator=(Array<T, M>&& other)
{
    if (&other != this)
    {
        Array::clear();
        if (appendMove(other.items, other.size()))
        {
            other.clear();
        }
    }
    return *this;
}

template <typename T, int N>
bool SC::Array<T, N>::push_back(const T& element)
{
    T* oldItems = items;
    return Operations::push_back(oldItems, element);
}

template <typename T, int N>
bool SC::Array<T, N>::push_back(T&& element)
{
    T* oldItems = items;
    return Operations::push_back(oldItems, move(element));
}

template <typename T, int N>
bool SC::Array<T, N>::pop_back()
{
    return Operations::pop_back(items);
}

template <typename T, int N>
bool SC::Array<T, N>::pop_front()
{
    return Operations::pop_front(items);
}

template <typename T, int N>
bool SC::Array<T, N>::resize(size_t newSize, const T& value)
{
    T* oldItems = items;
    return Operations::template resizeInternal<true>(oldItems, newSize, &value);
}

template <typename T, int N>
bool SC::Array<T, N>::resizeWithoutInitializing(size_t newSize)
{
    T* oldItems = items;
    return Operations::template resizeInternal<false>(oldItems, newSize, nullptr);
}

template <typename T, int N>
bool SC::Array<T, N>::insert(size_t idx, Span<const T> data)
{
    T* oldItems = items;
    return Operations::template insert<true>(oldItems, idx, data.data(), data.sizeInElements());
}

template <typename T, int N>
bool SC::Array<T, N>::append(Span<const T> data)
{
    T* oldItems = items;
    return Operations::template insert<true>(oldItems, size(), data.data(), data.sizeInElements());
}

template <typename T, int N>
template <typename U>
bool SC::Array<T, N>::append(Span<const U> data)
{
    T* oldItems = items;
    return Operations::template insert<true>(oldItems, size(), data.data(), data.sizeInElements());
}

template <typename T, int N>
template <typename U>
bool SC::Array<T, N>::appendMove(U&& src)
{
    if (appendMove({src.data(), src.size()}))
    {
        src.clear();
        return true;
    }
    return false;
}

template <typename T, int N>
template <typename U>
bool SC::Array<T, N>::contains(const U& value, size_t* foundIndex) const
{
    return SegmentItems<T>::findIf(
        items, 0, size(), [&](const T& element) { return element == value; }, foundIndex);
}

template <typename T, int N>
template <typename Lambda>
bool SC::Array<T, N>::find(Lambda&& lambda, size_t* foundIndex) const
{
    return SegmentItems<T>::findIf(items, 0, size(), forward<Lambda>(lambda), foundIndex);
}

template <typename T, int N>
bool SC::Array<T, N>::removeAt(size_t index)
{
    return SegmentItems<T>::removeAt(items, index);
}

template <typename T, int N>
template <typename Lambda>
bool SC::Array<T, N>::removeAll(Lambda&& criteria)
{
    return SegmentItems<T>::removeAll(items, 0, size(), forward<Lambda>(criteria));
}

template <typename T, int N>
template <typename U>
bool SC::Array<T, N>::remove(const U& value)
{
    return SegmentItems<T>::removeAll(items, 0, size(), [&](const auto& it) { return it == value; });
}

template <typename T, int N>
bool SC::Array<T, N>::appendMove(Span<T> data)
{
    T* oldItems = items;
    return Operations::template insert<false>(oldItems, size(), data.data(), data.sizeInElements());
}

template <typename T, int N>
bool SC::Array<T, N>::insertMove(size_t idx, T* src, size_t srcSize)
{
    T* oldItems = items;
    return Operations::template insert<false>(oldItems, idx, src, srcSize);
}
