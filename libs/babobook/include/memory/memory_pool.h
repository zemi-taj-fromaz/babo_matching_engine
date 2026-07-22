//
// Created by hrcol on 1.7.2026..
//

#ifndef BABOMATCHINGENGINE_MEMORY_POOL_H
#define BABOMATCHINGENGINE_MEMORY_POOL_H

#include <cstddef>
#include <memory>
#include <vector>
#include <utility>
#include <unordered_set>

namespace babo::memory
{


template <typename T>
union MemChunk
{
    alignas(T) std::byte storage[sizeof(T)];
    MemChunk* next;

    constexpr MemChunk() noexcept : next(nullptr) {}
};


template <typename T, size_t N = 1024>
class AllocatorPool
{
    static_assert(N >= 2, "AllocatorPool N == 1 flexible-block mode is not implemented");

public:

    ~AllocatorPool()
    {
        for (MemChunk<T>* chunk : chunkList)
            delete[] chunk;  // Use delete[] to match new[]
    }

    void reserve(size_t reserveSize)
    {
        if (reserveSize < size) return;

        while (reserveSize >= size)
        {
            auto newBlock = new MemChunk<T>[N];
            chunkList.push_back(newBlock);
            size += N;
        }
    }


    template <typename... Args>
    T* allocate(Args&&... args)
    {

        if (freeList)
        {
            auto chunk = freeList;
            freeList = chunk->next;

            T* object = std::construct_at(
                reinterpret_cast<T*>(chunk->storage),
                std::forward<Args>(args)...);

            nbElements++;

            return object;
        }

        const size_t index = nbElements++;

        if (index >= size) reserve(index);

        // Track high-water mark for destructor cleanup
        if (index > maxAllocatedIndex)
            maxAllocatedIndex = index;

        MemChunk<T>* chunk = getChunk(index);

        return std::construct_at(
            reinterpret_cast<T*>(chunk->storage),
            std::forward<Args>(args)...);
    }

    template <typename... Args>
    std::pair<T*, size_t> allocateWithIndex(Args&&... args)
    {
        if (freeList)
        {
            auto chunk = freeList;
            freeList = chunk->next;

            T* ptr = std::construct_at(
                reinterpret_cast<T*>(chunk->storage),
                std::forward<Args>(args)...);

            nbElements++;

            // Find the index by calculating from chunk pointer
            size_t index = 0;
            for (size_t i = 0; i < size; i++)
            {
                if (getElement(i) == ptr)
                {
                    index = i;
                    break;
                }
            }

            return {ptr, index};
        }

        const size_t index = nbElements++;

        if (index >= size) reserve(index);

        // Track high-water mark for destructor cleanup
        if (index > maxAllocatedIndex)
            maxAllocatedIndex = index;

        MemChunk<T>* chunk = getChunk(index);
        T* ptr = std::construct_at(
            reinterpret_cast<T*>(chunk->storage),
            std::forward<Args>(args)...);

        return {ptr, index};
    }

    void release(T* pointer)
    {
        if (pointer != nullptr)
        {
            std::destroy_at(pointer);

            reinterpret_cast<MemChunk<T>*>(pointer)->next = freeList;
            freeList = reinterpret_cast<MemChunk<T>*>(pointer);

            nbElements--;
        }
    }

    [[nodiscard]] size_t getNbElements() const noexcept { return nbElements; }

    [[nodiscard]] size_t getSize() const noexcept { return size; }

    void destroyAll()
    {
        if (maxAllocatedIndex == 0)
            return;

        // Build a set of free-list pointers for O(1) membership testing.
        std::unordered_set<MemChunk<T>*> freeSet;
        MemChunk<T>* current = freeList;
        while (current != nullptr)
        {
            freeSet.insert(current);
            current = current->next;
        }

        // Iterate through all allocated indices and destroy objects not in free list
        for (size_t i = 0; i <= maxAllocatedIndex && i < size; ++i)
        {
            MemChunk<T>* chunk = getChunk(i);
            if (freeSet.find(chunk) == freeSet.end())
            {
                // This object is still allocated, destroy it
                T* obj = std::launder(reinterpret_cast<T*>(chunk->storage));
                std::destroy_at(obj);
            }
        }

        // Reset pool state - all objects are now destroyed
        nbElements = 0;
        freeList = nullptr;
    }

    T* getSlot(size_t index) const
    {
        return std::launder(reinterpret_cast<T*>(getChunk(index)->storage));
    }

    void advanceCount(size_t n)
    {
        if (n == 0) return;
        const size_t newMax = nbElements + n - 1;
        if (newMax > maxAllocatedIndex)
            maxAllocatedIndex = newMax;
        nbElements += n;
    }

    inline T* getElement(size_t index) const
    {
        if (index >= size)
        {
            return nullptr;
        }

        return std::launder(reinterpret_cast<T*>(getChunk(index)->storage));
    }

protected:

    inline MemChunk<T>* getChunk(size_t index) const
    {
        return &chunkList[index / N][index % N];
    }

private:
    size_t size {0};
    size_t nbElements{0};

    /* High-water mark - highest index ever allocated */
    size_t maxAllocatedIndex {0};

    /* Pointer to the next free object in the pool */
    MemChunk<T>* freeList {nullptr};

    /* MemChunk lists used by the pool and retained for block destruction. */
    std::vector<MemChunk<T>*> chunkList;
};

}
#endif //BABOMATCHINGENGINE_MEMORY_POOL_H
