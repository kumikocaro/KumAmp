#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <memory>

namespace kumamp
{
/** Single-producer / single-consumer queue of trivially copyable items,
    built on juce::AbstractFifo. push() and pop() never allocate or lock. */
template <typename T, int Capacity>
class SpscQueue
{
public:
    static_assert (std::is_trivially_copyable_v<T>, "SpscQueue items must be POD-like");

    bool push (const T& item) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0) { items[(size_t) scope.startIndex1] = item; return true; }
        if (scope.blockSize2 > 0) { items[(size_t) scope.startIndex2] = item; return true; }
        return false;
    }

    bool pop (T& out) noexcept
    {
        const auto scope = fifo.read (1);
        if (scope.blockSize1 > 0) { out = items[(size_t) scope.startIndex1]; return true; }
        if (scope.blockSize2 > 0) { out = items[(size_t) scope.startIndex2]; return true; }
        return false;
    }

    int getNumReady() const noexcept { return fifo.getNumReady(); }
    int getFreeSpace() const noexcept { return fifo.getFreeSpace(); }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, (size_t) Capacity> items {};
};

/**
    Hands immutable objects (render plans, scene banks, NAM models, IRs...)
    from a non-realtime thread to the audio thread without locks.

    - publish()          : producer thread. Replaces any not-yet-consumed object.
    - acquire()          : audio thread. Swaps in the newest object, if any, and
                           pushes the retired one onto a lock-free garbage FIFO.
    - collectGarbage()   : message thread. Deletes retired objects.

    The audio thread never calls delete. If the garbage FIFO is full it simply
    keeps the current object until the message thread has drained it.
*/
template <typename T, int GarbageCapacity = 32>
class RTObjectPublisher
{
public:
    RTObjectPublisher() = default;
    ~RTObjectPublisher()
    {
        collectGarbage();
        delete pending.exchange (nullptr);
        delete current;
    }

    void publish (std::unique_ptr<T> object)
    {
        delete pending.exchange (object.release(), std::memory_order_acq_rel);
    }

    bool hasPending() const noexcept { return pending.load (std::memory_order_acquire) != nullptr; }

    T* acquire() noexcept
    {
        if (pending.load (std::memory_order_relaxed) != nullptr && garbage.getFreeSpace() > 0)
        {
            if (T* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (current != nullptr)
                    garbage.push (current);
                current = next;
            }
        }
        return current;
    }

    /** Audio thread only. */
    T* getCurrent() const noexcept { return current; }

    void collectGarbage()
    {
        T* dead = nullptr;
        while (garbage.pop (dead))
            delete dead;
    }

    /** Only while the audio thread is guaranteed not to run (prepareToPlay). */
    void resetImmediately (std::unique_ptr<T> object)
    {
        collectGarbage();
        delete pending.exchange (nullptr);
        delete current;
        current = object.release();
    }

private:
    std::atomic<T*> pending { nullptr };
    T* current = nullptr;
    SpscQueue<T*, GarbageCapacity> garbage;

    JUCE_DECLARE_NON_COPYABLE (RTObjectPublisher)
};

/**
    Like RTObjectPublisher, but the audio thread keeps the outgoing object alive
    while it crossfades to the incoming one (NAM model swaps, etc.).

    Audio thread:  if (beginSwap()) ...; use getCurrent() / getPrevious();
                   endCrossfade() once the fade is done.
    Loader thread: publish().
    Message thread: collectGarbage().
*/
template <typename T, int GarbageCapacity = 8>
class RTCrossfadeSlot
{
public:
    RTCrossfadeSlot() = default;
    ~RTCrossfadeSlot()
    {
        collectGarbage();
        delete pending.exchange (nullptr);
        delete previous;
        delete current;
    }

    void publish (std::unique_ptr<T> object)
    {
        delete pending.exchange (object.release(), std::memory_order_acq_rel);
    }

    /** Audio thread. True when a new object became current; the old one (if any)
        is available through getPrevious() until endCrossfade(). */
    bool beginSwap() noexcept
    {
        if (previous != nullptr || pending.load (std::memory_order_relaxed) == nullptr || garbage.getFreeSpace() < 1)
            return false;

        T* next = pending.exchange (nullptr, std::memory_order_acq_rel);
        if (next == nullptr)
            return false;

        previous = current;
        current = next;
        return true;
    }

    void endCrossfade() noexcept
    {
        if (previous != nullptr && garbage.push (previous))
            previous = nullptr;
    }

    T* getCurrent() const noexcept  { return current; }
    T* getPrevious() const noexcept { return previous; }

    void collectGarbage()
    {
        T* dead = nullptr;
        while (garbage.pop (dead))
            delete dead;
    }

    /** Only while the audio thread is not running (prepareToPlay). */
    void dropPendingAndPreviousWhileStopped()
    {
        delete pending.exchange (nullptr);
        delete previous;
        previous = nullptr;
        collectGarbage();
    }

private:
    std::atomic<T*> pending { nullptr };
    T* current = nullptr;
    T* previous = nullptr;
    SpscQueue<T*, GarbageCapacity> garbage;

    JUCE_DECLARE_NON_COPYABLE (RTCrossfadeSlot)
};

/** Shared background thread for file loading (NAM models, IRs, ...). */
struct FileLoaderPool
{
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("KumAmp loader").withNumberOfThreads (1) };
};
} // namespace kumamp
