// Copyright (c) 2012-2021 Wojciech Figat. All rights reserved.

#pragma once

#include "Engine/Core/Memory/Allocation.h"
#include "Engine/Platform/Platform.h"

template<typename T, typename AllocationType = HeapAllocation>
API_CLASS(InBuild) class FixedPool
{
public:
    typedef T ItemType;
    typedef typename AllocationType::template Data<T> AllocationData;

private:
    int64 _capacity;
    int64 _numAllocated;
    AllocationData _allocation;

public:
    /// <summary>
    /// Initializes a new instance of the <see cref="FixedPool"/> class.
    /// </summary>
    FORCE_INLINE FixedPool()
        : _capacity(0), _numAllocated(0)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="FixedPool"/> class.
    /// </summary>
    FORCE_INLINE FixedPool(uint64 capacity)
        : _capacity(capacity), _numAllocated(0)
    {
        ASSERT(capacity > 0);

        uint64 size = capacity * sizeof(ItemType);

        // Allocate data
        _allocation.Allocate(size);

        // Required. We must have totally clean memory for our pool.
        Platform::MemoryClear((void*)_allocation.Get(), size);
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="FixedPool"/> class.
    /// </summary>
    /// <param name="other">The other collection to move.</param>
    FORCE_INLINE FixedPool(FixedPool&& other) noexcept
    {
        _capacity = other._capacity;
        _numAllocated = other._numAllocated;
        other._capacity = 0;
        other._numAllocated = 0;
        _allocation.Swap(other._allocation);
    }

    FixedPool(const FixedPool& other) = delete;
    FixedPool& operator=(const FixedPool& other) = delete;
    FixedPool& operator=(FixedPool&& other) = delete;

    /// <summary>
    /// Finalizes an instance of the <see cref="FixedPool"/> class.
    /// </summary>
    ~FixedPool()
    {
        _allocation.Free();
    }

public:
    /// <summary>
    /// Acquires new free item from this pool.
    /// </summary>
    /// <returns>The acquired item, or -1 when no free indices are left.</returns>
    bool TryAcquire(ItemType* item)
    {
        // TODO

        return false;
    }

    /// <summary>
    /// Returns the item to the pool.
    /// </summary>
    void Release(ItemType item)
    {
        // TODO
    }
};
