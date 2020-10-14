//--------------------------------------------------------------------------------------------------
// 
//	LOGERR
//
//--------------------------------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
// and associated documentation files (the "Software"), to deal in the Software without 
// restriction, including without limitation the rights to use, copy, modify, merge, publish, 
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the 
// Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all copies or 
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//--------------------------------------------------------------------------------------------------
//
// Copyright (c) 2014 Nic Holthaus
// 
//--------------------------------------------------------------------------------------------------
//
// ATTRIBUTION:
//
//
//--------------------------------------------------------------------------------------------------
//
///	@file			concurrent_queue.hpp
///	@brief			A queue implementation that provides thread-safe access with reasonable exception safety
/// @details		Uses C++11
//
//--------------------------------------------------------------------------------------------------

#ifndef concurrent_queue_h__
#define concurrent_queue_h__

//------------------------
//	INCLUDES
//------------------------
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>

//	----------------------------------------------------------------------------
//	CLASS		ConcurrentQueue
//  ----------------------------------------------------------------------------
///	@brief		Thread-safe First-in-Fist-out (FIFO) queue.
///	@details	Intended for use in a multiple-producer, single-consumer application.\n
///				From Wikipedia(http://en.wikipedia.org/wiki/Queue_%28abstract_data_type%29):\n
///				In computer science, a queue is a particular kind
///				of abstract data type or collection in which the entities in the
///				collection are kept in order and the principal(or only) operations
///				on the collection are the addition of entities to the rear terminal
///				position, known as enqueue, and removal of entities from the front
///				terminal position, known as dequeue.This makes the queue a First-In-First-Out(FIFO)
///				data structure.In a FIFO data structure,
///				the first element added to the queue will be the first one to be
///				removed.This is equivalent to the requirement that once a new
///				element is added, all elements that were added before have to be
///				removed before the new element can be removed. Often a peek or
///				front operation is also entered, returning the value of the front
///				element without dequeuing it. A queue is an example of a linear
///				data structure, or more abstractly a sequential collection.
//  ----------------------------------------------------------------------------
template <typename T>
class ConcurrentQueue
{
public:

    /**
     * @brief		Constructor
     * @details		Creates a concurrent queue with a 0ms default pop timeout.
     */
    ConcurrentQueue() 
        : m_timeout(0)
    {

    }

    /**
     * @brief		destructor
     * @details
     */
    virtual ~ConcurrentQueue()
    {

    }

    /**
     * @brief		pops (dequeues) the front (first) item in the queue.
     * @details		<b>Complexity:</b> O(1) if the queue is not empty, otherwise up to as long as
     *				the timeout (10ms default). Pop uses a conditional wait which uses little to no
     *				resources while waiting for a non-empty state. There is no 'empty' function or
     *				other way to check the queue state in order to ensure that popping is an atomic
     *				operation.\n
     *				<b>Iterator Validity:</b> N/A. A concurrent queue cannot be iterated except by
     *				popping elements one at a time.\n
     *				<b>Data Races:</b> The container is modified. Concurrently pushing elements is safe.
     *				Concurrently popping the queue from multiple threads will result in a data race.
     *				The queue is intended for use by a single consumer.\n
     *				<b>Exception Safety:</b> If an exception is thrown, the queue is in a valid state (basic guarantee).\n
     * @param[out]	item - location to which the front of the queue will be popped.
     * @returns		true on success, false on failure (i.e. a timeout occurred).
     */
    bool pop(T& item)
    {
        std::unique_lock<std::mutex> mlock(mutex);
        while (queue.empty())
        {
            if (notEmpty.wait_for(mlock, m_timeout) == std::cv_status::timeout)
                return false;
        }
        item = std::move(queue.front());
        queue.pop_front();
        return true;
    }

    /**
     * @brief		Emplaces (enqueues) an item onto the back (end) of the queue.
     * @details		The item is constructed in-place by calling its constructor with <i>args</i> forwarded.\n
     *				<b> Complexity:</b> Constant time (so long as the items constructor is constant time).\n
     *				<b> Iterator Validity:</b> N/A. A concurrent queue cannot be iterated except by
     *				popping elements one at a time.\n
     *				<b> Data Races:</b> The container is modified. It is safe to emplace items from
     *				multiple threads.\n
     *				<b> Exception Safety:</b> If an exception is thrown, the queue is in a valid state (basic guarantee).\n
     * @param[in]	args - arguments to be forwarded to the new items constructor.
     */
    template<class... Args>
    void emplace(Args&&... args)
    {
        std::unique_lock<std::mutex> mlock(mutex);
        queue.emplace_back(std::forward<Args>(args)...);
        mlock.unlock();
        notEmpty.notify_one();
    }

    /**
     * @brief		Pushes (enqueues) an item onto the back (end) of the queue.
     * @details		Copies/Moves the item.\n
     *				<b> Complexity:</b> Constant time.\n
     *				<b> Iterator Validity:</b> N/A. A concurrent queue cannot be iterated except by
     *				popping elements one at a time.\n
     *				<b> Data Races:</b> The container is modified. It is safe to push items from
     *				multiple threads.\n
     *				<b> Exception Safety:</b> If an exception is thrown, the queue is in a valid state (basic guarantee).\n
     * @param[in]	item - item to be enqueued.
     */
    template<typename U>
    void push(U&& item)
    {
        std::unique_lock<std::mutex> mlock(mutex);
        queue.push_back(std::forward<U>(item));
        mlock.unlock();
        notEmpty.notify_one();
    }

    /**
     * @brief		Removes all elements from the queue.
     * @details		The queue becomes empty upon termination of this function.\n
     *				<b> Complexity:</b> Linear time (destructions).\n
     *				<b> Iterator Validity:</b> All iterators are invalidated.
     *				<b> Data Races:</b> The container is modified. It is safe to push items from
     *				multiple threads.\n
     *				<b> Exception Safety:</b> If an exception is thrown, the queue is in a valid state (basic guarantee).\n
     * @returns		void
     */
    void clear()
    {
        std::unique_lock<std::mutex> mlock(mutex);
        queue.clear();
    }

    /**
     * @brief		Sets the pop timeout failure interval to the given value.
     * @details
     * @param[in]	timeout - number of milliseconds the pop() function should wait before returning
     *				failure.\n
     */
    void setTimeout(std::chrono::milliseconds timeout)
    {
        m_timeout = timeout;
    }

private:

    std::deque<T>				queue;
    std::mutex					mutex;
    std::condition_variable		notEmpty;
    std::chrono::milliseconds	m_timeout;
};

#endif // concurrent_queue_h__