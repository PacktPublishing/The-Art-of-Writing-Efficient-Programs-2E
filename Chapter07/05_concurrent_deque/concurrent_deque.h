// Copyright (c) 2026 Fedor G. Pikus, fpikus@gmail.com
//  https://github.com/fpikus/LockFree
//
// MIT License
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
#ifndef INCLUDED_CONCURRENT_DEQUE_H
#define INCLUDED_CONCURRENT_DEQUE_H

#include <atomic>
#include <vector>
#include <cstddef>
#include <type_traits>
#include <new>
#include <stdexcept>
#include <utility>
#include <mutex>
#include <bit>
#include <iterator>
#include <memory>
#include "spinlock.h"

/**
 * @brief A thread-safe, append-only segmented array (deque-like container)
 * optimized for wait-free reads and locked writes.
 *
 * Architecture & Concurrency:
 * - Segmented Storage: Elements are stored in fixed-size blocks of `BlockSize` elements.
 *   This ensures that pointers/references to elements are never invalidated once constructed,
 *   which is critical for lock-free readers. Memory is allocated as uninitialized bytes,
 *   and elements are constructed in-place to avoid unnecessary default constructions.
 * - Bitwise Chunking: `BlockSize` is strictly enforced to be a power of 2. This allows the
 *   container to replace slow modulo (%) and division (/) operations with ultra-fast
 *   bitwise AND (&) and right-shift (>>) operations when resolving element indices.
 * - Directory Structure: An atomic pointer (`directory_`) holds an array of block pointers.
 *   Wait-free readers read this pointer directly to resolve their target block and offset.
 * - Retired Directories: When the container exceeds its directory capacity, a larger directory
 *   is allocated. The old directory is pushed into `retired_directories_` rather than being
 *   freed immediately. This ensures that a concurrent reader holding a stale directory pointer
 *   can still safely dereference it without causing a Use-After-Free (UAF).
 * - Synchronization: Readers are strictly wait-free. Writers use a lightweight `SpinLock`
 *   to serialize concurrent modifications (`push_back`, `emplace_back`, `resize`, `reserve`).
 *   The global element count (`size_`) is updated with `std::memory_order_release` as the
 *   absolute last step of any write operation. Readers fetch `size_` with `std::memory_order_acquire`,
 *   establishing a happens-before relationship that guarantees the visibility of the newly
 *   constructed element and its enclosing block pointer.
 *
 * @tparam T The type of elements stored in the container.
 * @tparam BlockSize The number of elements per allocated block segment. Must be a power of 2.
 */
template <typename T, size_t BlockSize = 1024>
class ConcurrentAppendDeque {
    static_assert(BlockSize > 0 && (BlockSize & (BlockSize - 1)) == 0, "BlockSize must be a power of 2");
    
    static constexpr size_t BlockMask = BlockSize - 1;
    static constexpr size_t BlockShift = std::countr_zero(BlockSize);

public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;

    /**
     * @brief Random-access iterator over the deque.
     *
     * The iterator's identity is its logical index (`index_`); all comparisons and
     * distance computations use the index alone. As an optimization, a direct pointer
     * to the current element (`current_element_`) is cached so that traversal within
     * a block is plain pointer arithmetic with no atomic directory load per element.
     *
     * Caching invariant: a non-null `current_element_` always equals
     * `&dir[index_ >> BlockShift][index_ & BlockMask]` for any directory `dir` — the
     * cache stays valid even if the directory is reallocated concurrently, because
     * blocks themselves never move once allocated. The cache only becomes stale when
     * the iterator crosses a block boundary (the next block lives at an unrelated
     * address), so boundary crossings null it out and the next dereference re-resolves
     * it from the current directory.
     */
    template <bool IsConst>
    class Iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = std::conditional_t<IsConst, const T*, T*>;
        using reference = std::conditional_t<IsConst, const T&, T&>;
        using deque_ptr = std::conditional_t<IsConst, const ConcurrentAppendDeque*, ConcurrentAppendDeque*>;

        Iterator() = default;

        // Constructs an iterator positioned at `index`. The element address is resolved
        // eagerly only when `index` refers to a published element; the size() check is
        // also what makes the resolution safe: it is the acquire load that guarantees the
        // directory (which is still nullptr for an empty deque) and the block contents
        // are visible. For end() and other past-the-end positions the cache starts null
        // and is resolved lazily on first dereference.
        Iterator(deque_ptr deque, size_t index) : deque_(deque), index_(index) {
            if (deque_ && index_ < deque_->size()) {
                T** dir = deque_->directory_.load(std::memory_order_acquire);
                current_element_ = &dir[index_ >> BlockShift][index_ & BlockMask];
            }
        }

        // Converting constructor: implicit iterator -> const_iterator conversion.
        // Declared as a constructor template so it is never a copy constructor and
        // cannot suppress the implicitly generated one; the constraint limits it to
        // the mutable-to-const direction.
        template <bool OtherConst>
            requires (IsConst && !OtherConst)
        Iterator(const Iterator<OtherConst>& other)
            : deque_(other.deque_), index_(other.index_), current_element_(other.current_element_) {}

        reference operator*() const {
            // Lazy re-resolution: the cache is null after a block-boundary crossing or for
            // an iterator created past the end and later moved back. Dereferencing is only
            // legal for index_ < size() observed by this thread, so the directory entry
            // read here is fully published.
            if (!current_element_) {
                T** dir = deque_->directory_.load(std::memory_order_acquire);
                current_element_ = &dir[index_ >> BlockShift][index_ & BlockMask];
            }
            return *current_element_;
        }

        pointer operator->() const {
            return &(**this);
        }

        reference operator[](difference_type n) const {
            return *(*this + n);
        }

        Iterator& operator++() {
            ++index_;
            if ((index_ & BlockMask) == 0) {
                // Entered a new block: its address is unrelated to the previous one,
                // so drop the cache and let operator* re-resolve from the directory.
                current_element_ = nullptr;
            } else if (current_element_) {
                ++current_element_; // Still inside the same contiguous block.
            }
            return *this;
        }

        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        Iterator& operator--() {
            if ((index_ & BlockMask) == 0) {
                // Currently at the first element of a block (or at index 0): stepping back
                // leaves the block, so the cache must be re-resolved.
                --index_;
                current_element_ = nullptr;
            } else {
                --index_;
                if (current_element_) --current_element_;
            }
            return *this;
        }

        Iterator operator--(int) {
            Iterator tmp = *this;
            --(*this);
            return tmp;
        }

        Iterator& operator+=(difference_type n) {
            if (n == 0) return *this;
            // Unsigned wraparound gives the correct result for negative n.
            size_t new_index = index_ + n;
            // The cache survives only if the jump lands in the same block.
            if (current_element_ && (index_ >> BlockShift) == (new_index >> BlockShift)) {
                current_element_ += n;
            } else {
                current_element_ = nullptr;
            }
            index_ = new_index;
            return *this;
        }

        Iterator& operator-=(difference_type n) {
            return *this += -n;
        }

        friend Iterator operator+(Iterator it, difference_type n) {
            it += n;
            return it;
        }

        friend Iterator operator+(difference_type n, Iterator it) {
            it += n;
            return it;
        }

        friend Iterator operator-(Iterator it, difference_type n) {
            it -= n;
            return it;
        }

        friend difference_type operator-(const Iterator& a, const Iterator& b) {
            return a.index_ - b.index_;
        }

        friend bool operator==(const Iterator& a, const Iterator& b) {
            return a.index_ == b.index_;
        }

        friend bool operator!=(const Iterator& a, const Iterator& b) {
            return a.index_ != b.index_;
        }

        friend bool operator<(const Iterator& a, const Iterator& b) {
            return a.index_ < b.index_;
        }

        friend bool operator<=(const Iterator& a, const Iterator& b) {
            return a.index_ <= b.index_;
        }

        friend bool operator>(const Iterator& a, const Iterator& b) {
            return a.index_ > b.index_;
        }

        friend bool operator>=(const Iterator& a, const Iterator& b) {
            return a.index_ >= b.index_;
        }

    private:
        // The two specializations must see each other's private members for the
        // converting constructor above.
        template <bool> friend class Iterator;

        deque_ptr deque_ = nullptr;              // Owning container; used only to load directory_
        size_t index_ = 0;                       // Logical index; sole basis for comparisons/distance
        mutable pointer current_element_ = nullptr; // Cached element address (see class comment);
                                                    // mutable because operator*() const resolves it lazily
    }; // class Iterator

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    ConcurrentAppendDeque() = default;

    ConcurrentAppendDeque(const ConcurrentAppendDeque&) = delete;
    ConcurrentAppendDeque& operator=(const ConcurrentAppendDeque&) = delete;
    ConcurrentAppendDeque(ConcurrentAppendDeque&&) = delete;
    ConcurrentAppendDeque& operator=(ConcurrentAppendDeque&&) = delete;

    iterator begin() { return iterator(this, 0); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator cbegin() const { return const_iterator(this, 0); }

    iterator end() { return iterator(this, size()); }
    const_iterator end() const { return const_iterator(this, size()); }
    const_iterator cend() const { return const_iterator(this, size()); }

    bool empty() const { return size() == 0; }
    
    size_t capacity() const {
        std::lock_guard lock(spinlock_);
        return directory_capacity_ << BlockShift;
    }

    T& front() { return (*this)[0]; }
    const T& front() const { return (*this)[0]; }
    
    T& back() { return (*this)[size() - 1]; }
    const T& back() const { return (*this)[size() - 1]; }

    T& at(size_t index) {
        if (index >= size()) throw std::out_of_range("Index out of range");
        return (*this)[index];
    }
    
    const T& at(size_t index) const {
        if (index >= size()) throw std::out_of_range("Index out of range");
        return (*this)[index];
    }

    /**
     * @brief Destroys the container, cleaning up all allocated blocks and retired directories.
     * 
     * Because old directories are preserved for the lifetime of the container to support
     * lock-free readers, they are finally deallocated here. Elements up to the current
     * size are explicitly destructed.
     *
     * @pre No other thread may access the container concurrently (as with any destructor);
     * this is why relaxed loads suffice here.
     */
    ~ConcurrentAppendDeque() {
        size_t current_size = size_.load(std::memory_order_relaxed);
        T** dir = directory_.load(std::memory_order_relaxed);
        if (dir) {
            // Destruct all constructed elements up to current_size.
            for (size_t i = 0; i < current_size; ++i) {
                size_t block_idx = i >> BlockShift;
                size_t local_offset = i & BlockMask;
                dir[block_idx][local_offset].~T();
            }
            // Deallocate all allocated blocks.
            for (size_t i = 0; i < directory_capacity_; ++i) {
                if (dir[i]) {
                    // dir[i] points at the first byte of the Block's `data` member, which has
                    // the same address as the Block itself (first member of a standard-layout
                    // struct), so this cast recovers the pointer returned by `new` in
                    // allocate_block() and delete matches the original allocation.
                    Block* b = reinterpret_cast<Block*>(dir[i]);
                    delete b;
                }
            } // loop over directory entries
            delete[] dir;
        }

        // Deallocate any old directories that were replaced during reallocations.
        for (T** retired : retired_directories_) {
            delete[] retired;
        }
    } // ~ConcurrentAppendDeque()

    /**
     * @brief Returns the number of elements in the container.
     * 
     * Uses std::memory_order_acquire. This is the crucial synchronization point for readers.
     * It ensures that any memory written by a writer before it updated the size
     * (including block allocations and object construction) is fully visible to the reader.
     */
    size_t size() const {
        return size_.load(std::memory_order_acquire);
    }

    /**
     * @brief Wait-free element access.
     * 
     * WARNING: The caller MUST ensure that `index < size()`. The `size()` must have
     * been called prior to this to establish the memory acquire barrier.
     * Loading the directory pointer uses std::memory_order_acquire to ensure it sees
     * the most recent fully populated directory pointer array.
     *
     * Why any directory loaded here is safe for every index below the observed size:
     * - It cannot be too OLD: the writer stores directory_ before the release store of
     *   size_, so once the caller's acquire load of size_ observes the new size, write-read
     *   coherence forbids this load from returning an earlier directory value.
     * - It CAN be too new (a concurrent reallocation may have swapped directories since
     *   size() was read), but that is harmless: a new directory starts as a superset of the
     *   old one's block pointers, and blocks never move, so every index < size() still maps
     *   to the same live element.
     */
    T& operator[](size_t index) {
        T** dir = directory_.load(std::memory_order_acquire);
        size_t block_idx = index >> BlockShift;
        size_t local_offset = index & BlockMask;
        return dir[block_idx][local_offset];
    }

    const T& operator[](size_t index) const {
        T** dir = directory_.load(std::memory_order_acquire);
        size_t block_idx = index >> BlockShift;
        size_t local_offset = index & BlockMask;
        return dir[block_idx][local_offset];
    }

    /**
     * @brief Ensures that the directory has enough capacity for `new_capacity` elements.
     * 
     * Serialized by the spinlock. Does not allocate the blocks themselves (lazy allocation),
     * only expands the array of block pointers if necessary.
     */
    void reserve(size_t new_capacity) {
        std::lock_guard lock(spinlock_);
        size_t current_capacity = directory_capacity_ << BlockShift;
        if (new_capacity > current_capacity) {
            size_t new_dir_capacity = (new_capacity + BlockMask) >> BlockShift;
            reallocate_directory(new_dir_capacity);
        }
        // No size_ update is needed to publish the new directory: readers synchronize on
        // directory_ itself (release store in reallocate_directory(), acquire load in
        // operator[]), and any directory a reader observes — old or new — is valid for
        // every index below the size it has seen.
    } // reserve()

    /**
     * @brief Resizes the container to contain `new_size` elements.
     * 
     * If `new_size` is greater than the current size, elements are default-constructed.
     * If `new_size` is less than or equal to the current size, the call is a safe no-op.
     * 
     * NOTE: We acquire the current size to ensure that even if this call is a no-op,
     * the thread executing `resize()` establishes a happens-before relationship with 
     * previous writers. This allows the caller to safely assume that elements up to
     * `new_size` are fully visible without requiring an explicit `size()` check.
     */
    void resize(size_t new_size) {
        // First check (lock-free) with acquire semantics.
        // If the size is already large enough, the acquire load synchronizes-with
        // the release store of the thread that did the resizing, ensuring memory visibility.
        size_t current_size = size_.load(std::memory_order_acquire);
        if (new_size <= current_size) {
            return;
        }

        std::lock_guard lock(spinlock_);
        // Second check (locked)
        current_size = size_.load(std::memory_order_relaxed);
        if (new_size > current_size) {
            size_t new_dir_capacity = (new_size + BlockMask) >> BlockShift;
            if (new_dir_capacity > directory_capacity_) {
                reallocate_directory(new_dir_capacity);
            }
            T** dir = directory_.load(std::memory_order_relaxed);
            for (size_t i = current_size; i < new_size; ++i) {
                size_t block_idx = i >> BlockShift;
                size_t local_offset = i & BlockMask;
                if (!dir[block_idx]) {
                    dir[block_idx] = allocate_block();
                }
                std::construct_at(&dir[block_idx][local_offset]); // Value-initialization (zero for scalar T)
            } // loop constructing elements [current_size, new_size)
            // Release store to make new elements visible via size_
            size_.store(new_size, std::memory_order_release);
        } // if growth still needed after re-check under lock
    } // resize()

    /**
     * @brief Appends a copy of `value` to the end of the container.
     * Forwards to emplace_back(), which implements the full write protocol.
     */
    void push_back(const T& value) { emplace_back(value); }

    /**
     * @brief Appends `value` to the end of the container by move.
     * Forwards to emplace_back(), which implements the full write protocol.
     */
    void push_back(T&& value) { emplace_back(std::move(value)); }

    /**
     * @brief Appends a new element constructed in-place to the end of the container.
     * Implements the write protocol shared by both push_back() overloads.
     *
     * Serialized by the spinlock. Allocates a new block if the target index falls
     * outside currently allocated blocks.
     *
     * The plain (non-atomic) store into the directory entry below is race-free with
     * concurrent wait-free readers: the entry is written only when it is null, which
     * means every slot of that block lies at index >= current_size — no reader may
     * legally touch it until the release store of size_ publishes the element, and that
     * store also carries the entry to any reader that acquires the new size. The same
     * argument covers the block-pointer store in resize().
     *
     */
    template<typename... Args>
    void emplace_back(Args&&... args) {
        std::lock_guard lock(spinlock_);
        size_t current_size = size_.load(std::memory_order_relaxed);
        size_t block_idx = current_size >> BlockShift;
        size_t local_offset = current_size & BlockMask;

        if (block_idx >= directory_capacity_) {
            size_t new_capacity = directory_capacity_ == 0 ? 1 : directory_capacity_ * 2;
            reallocate_directory(new_capacity);
        }

        T** dir = directory_.load(std::memory_order_relaxed);
        if (!dir[block_idx]) {
            dir[block_idx] = allocate_block();
        }

        // Construct element in uninitialized memory
        std::construct_at(&dir[block_idx][local_offset], std::forward<Args>(args)...);
        
        // Absolute last instruction: release store to make the new element visible.
        size_.store(current_size + 1, std::memory_order_release);
    } // emplace_back()

private:
    // Represents a chunk of uninitialized memory with proper alignment for T.
    struct alignas(T) Block {
        unsigned char data[BlockSize * sizeof(T)];
    };

    /**
     * @brief Allocates a new directory, copies existing block pointers, and retires the old directory.
     * 
     * We never delete the old directory immediately because a concurrent reader thread
     * might have just loaded the old directory pointer but hasn't accessed it yet.
     * 
     * @pre The caller MUST hold `spinlock_` to ensure thread-safe modification of 
     * `directory_capacity_` and to prevent concurrent overlapping reallocations.
     */
    void reallocate_directory(size_t new_capacity) {
        T** new_dir = new T*[new_capacity];
        T** old_dir = directory_.load(std::memory_order_relaxed);
        
        for (size_t i = 0; i < directory_capacity_; ++i) {
            new_dir[i] = old_dir[i];
        }
        for (size_t i = directory_capacity_; i < new_capacity; ++i) {
            new_dir[i] = nullptr;
        }

        // Store new directory with release semantics so readers loading it see the populated pointers.
        directory_.store(new_dir, std::memory_order_release);
        if (old_dir) {
            retired_directories_.push_back(old_dir);
        }
        directory_capacity_ = new_capacity;
    } // reallocate_directory()

    /**
     * @brief Allocates a new uninitialized memory block for elements.
     * 
     * @pre The caller MUST hold `spinlock_` to ensure the newly allocated block 
     * is exclusively assigned to the directory without race conditions.
     */
    T* allocate_block() {
        // Default-initialization (`new Block` with no parentheses) is deliberate: it leaves
        // the byte array untouched. `new Block()` would value-initialize the struct and
        // memset the entire block to zero while holding the spinlock, silently defeating
        // the uninitialized-storage design documented on the class.
        Block* b = new Block;
        return reinterpret_cast<T*>(b->data);
    }

    std::atomic<T**> directory_ {};            // Atomic pointer to current array of block pointers
    size_t directory_capacity_ {};             // Current capacity (in blocks) of directory_
    std::vector<T**> retired_directories_ {};  // Old directories preserved for wait-free reader safety
    std::atomic<size_t> size_ {};              // Global element count
    mutable SpinLock spinlock_;                // Serializes all write operations
}; // class ConcurrentAppendDeque

#endif // INCLUDED_CONCURRENT_DEQUE_H
