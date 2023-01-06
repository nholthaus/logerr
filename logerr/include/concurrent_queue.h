//--------------------------------------------------------------------------------------------------
//
//	QUEUE
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
// Copyright (c) 2015 Nic Holthaus
//
//--------------------------------------------------------------------------------------------------
//
// ATTRIBUTION:
//  - https://en.cppreference.com/w/cpp/container/deque
//  - https://stackoverflow.com/questions/29986208/how-should-i-deal-with-mutexes-in-movable-types-in-c/29988626
//
//--------------------------------------------------------------------------------------------------
//
//  TODO:
//  - Write a `concept` for what constitutes a queue type
//  - `pop` should use an upgradeable mutex, if the C++ standard committee ever creates one.
//
//--------------------------------------------------------------------------------------------------
//
/// @file	concurrent_queue.h
/// @brief	A thread-safe queue (FIFO) implementation
//
//--------------------------------------------------------------------------------------------------

#pragma once
#ifndef concurrent_queue_h__
#define concurrent_queue_h__

//----------------------------
//  INCLUDES
//----------------------------

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>

//-------------------------
//	FORWARD DECLARATIONS
//-------------------------

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: concurrent_queue
//----------------------------------------------------------------------------------------------------------------------
/// @brief The `concurrent_queue` class is a sequence container class that allows first-in, first-out access to its elements.
/// @details It enables a limited set of concurrency-safe operations, such as push and try_pop. Here, concurrency-safe means pointers
///          or iterators are always valid. It's not a guarantee of element initialization, or of a particular traversal order.
/// @tparam T The data type of the elements to be stored in the queue.
/// @tparam Queue Underlying concurrent queue data structure. Defaults to `std::deque`.
/// @tparam AllocThe type that represents the stored allocator object that encapsulates details about the allocation and deallocation
///         of memory for this concurrent queue. This argument is optional and the default value is allocator<T>.
template<class T, template<class, class> class Queue = std::deque, class Alloc = std::allocator<T>>
class concurrent_queue
{
public:
	//----------------------------
	//  TYPEDEFS
	//----------------------------

	typedef T                                                             value_type;                ///< A type that represents the data type stored in a concurrent queue.
	typedef Alloc                                                         allocator_type;            ///< A type that represents the allocator class for the concurrent queue.
	typedef value_type&                                                   reference;                 ///< A type that provides a reference to an element stored in a concurrent queue.
	typedef std::condition_variable_any                                   condition_type;            ///< A type that provides a waitable condition of the concurrent queue.
	typedef const value_type&                                             const_reference;           ///< A type that provides a reference to a const element stored in a concurrent queue for reading and performing const operations.
	typedef typename std::allocator_traits<allocator_type>::pointer       pointer;                   ///< A type that provides a pointer to an element stored in a concurrent queue.
	typedef typename std::allocator_traits<allocator_type>::const_pointer const_pointer;             ///< A type that provides a const pointer to an element stored in a concurrent queue.
	typedef Queue<T, Alloc>                                               queue_type;                ///< A type that represents the underlying non-thread-safe data structure for the concurrent queue
	typedef typename queue_type::iterator                                 iterator;                  ///< A type that represents a non-thread-safe iterator over the elements in a concurrent queue.
	typedef typename queue_type::const_iterator                           const_iterator;            ///< A type that represents a non-thread-safe const iterator over elements in a concurrent queue.
	typedef typename std::reverse_iterator<iterator>                      reverse_iterator;          ///< A type that represents a reverse non-thread-safe iterator over the elements in a concurrent queue.
	typedef typename std::reverse_iterator<const_iterator>                const_reverse_iterator;    ///< A type that represents a reverse non-thread-safe const iterator over elements in a concurrent queue.
	typedef std::shared_timed_mutex                                       mutex_type;                ///< A type that represents the mutex protecting the concurrent queue.
	typedef typename std::shared_lock<mutex_type>                         read_lock_type;            ///< A type representing a lock on the concurrent queue's mutex which is sufficient to read data in a thread-safe manner.
	typedef typename std::unique_lock<mutex_type>                         write_lock_type;           ///< A type representing a lock on the concurrent queue's mutex which is sufficient to write data in a thread-safe manner.
	typedef typename std::iterator_traits<iterator>::difference_type      difference_type;           ///< A type that provides the signed distance between two elements in a concurrent queue.
	typedef size_t                                                        size_type;                 ///< A type that counts the number of elements in a concurrent queue.

public:
	//----------------------------
	//  CONSTRUCTORS
	//----------------------------

	/// @{
	/// @name Constructors

	/// @brief Default Constructor.
	/// @details Constructs an empty container, with no elements.
	/// @param[in] alloc optional memory allocator.
	inline concurrent_queue() = default;

	/// @brief Default Constructor.
	/// @details Constructs an empty container, with no elements.
	/// @param[in] alloc optional memory allocator.
	inline explicit concurrent_queue(const allocator_type& alloc)
			: queue(alloc)
	{
	}

	/// @brief Fill Constructor
	/// @details Constructs a container with n elements. Each element is a copy of val (if provided).
	/// @param[in] n number of elements
	/// @param[in] alloc optional memory allocator.
	inline explicit concurrent_queue(size_type n, const allocator_type& alloc = allocator_type())
			: queue(n, alloc)
	{
	}

	/// @brief Fill Constructor
	/// @details Constructs a container with n elements. Each element is a copy of val (if provided).
	/// @param[in] n number of elements
	/// @param[in] val value to fill the concurrent queue with
	/// @param[in] alloc optional memory allocator.
	inline concurrent_queue(size_type n, const value_type& val, const allocator_type& alloc = allocator_type())
			: queue(n, val, alloc)
	{
	}

	/// @brief Range Constructor
	/// @details Constructs a container with as many elements as the range [first,last), with each element
	///          emplace-constructed from its corresponding element in that range, in the same order.
	/// @tparam InputIterator Input Iterator to value_type
	/// @param[in] first Iterator to the first element of the range
	/// @param[in] last Iterator to the one-past-last element of the range
	template<typename InputIterator>
	inline concurrent_queue(InputIterator first, InputIterator last, const allocator_type& alloc = allocator_type())
			: queue(first, last, alloc)
	{
	}

	/// @brief Copy Constructor
	/// @details Constructs a container with a copy of each of the elements in x, in the same order. Thread-safe.
	/// @param[in] other queue to copy
	inline concurrent_queue(const concurrent_queue& other)
			: concurrent_queue(other, read_lock_type(other.mutex))
	{
	}

	/// @brief Copy Constructor with allocator
	/// @details Constructs a container with a copy of each of the elements in x, in the same order. Thread-safe.
	/// @param[in] other queue to copy
	/// @param[in] alloc optional memory allocator.
	inline concurrent_queue(const concurrent_queue& other, const allocator_type& alloc)
			: concurrent_queue(other, alloc, read_lock_type(other.mutex))
	{
	}

	/// @brief Move Constructor
	/// @details Constructs a container that acquires the elements of `other`. Ownership of the contained elements is directly
	///          transferred. `other` is left in an unspecified but valid state.
	/// @param[in] other container to move from
	inline concurrent_queue(concurrent_queue&& other) noexcept
			: concurrent_queue(std::move(other), write_lock_type(other.mutex))
	{
	}

	/// @brief Move Constructor
	/// @details Constructs a container that acquires the elements of `other`. Ownership of the contained elements is directly
	///          transferred. `other` is left in an unspecified but valid state.
	/// @param[in] other container to move from
	inline concurrent_queue(concurrent_queue&& other, const allocator_type& alloc) noexcept
			: concurrent_queue(std::move(other), alloc, write_lock_type(other.mutex))
	{
	}

	/// @brief Initializer List Constructor
	/// @param[in] init  	initializer list to initialize the elements of the container with
	/// @param[in] alloc 	allocator to use for all memory allocations of this container
	inline concurrent_queue(std::initializer_list<T> init, const allocator_type& alloc = allocator_type())
			: concurrent_queue(init.begin(), init.end(), alloc)
	{
	}

	/// @}

	//----------------------------
	//  DESTRUCTOR
	//----------------------------

	/// @{
	/// @name Destructor

	/// @brief Destructor
	/// @details Destructs the concurrent queue. The destructors of the elements are called and the used storage is deallocated.
	///          Note, that if the elements are pointers, the pointed-to objects are not destroyed.
	inline ~concurrent_queue() = default;

	/// @}

	//----------------------------
	//  ASSIGNMENT OPERATORS
	//----------------------------
	/// @{
	/// @name Assignment Operators

	/// @brief Copy Assignment Operator
	/// @param[in] other concurrent queue whose elements are to be copied.
	/// @return A reference to this concurrent queue.
	inline concurrent_queue& operator=(const concurrent_queue& other)
	{
		// acquire appropriate locks on both containers
		write_lock_type  lock_this(this->mutex, std::defer_lock);
		read_lock_type   lock_that(other.mutex, std::defer_lock);
		std::scoped_lock lock(lock_this, lock_that);

		queue = other.queue;
		new_element.notify_all();
		return *this;
	}

	/// @brief Move Assignment Operator
	/// @param[in] other  concurrent queue whose elements are to be moved.
	/// @return A reference to this concurrent queue.
	inline concurrent_queue& operator=(concurrent_queue&& other) noexcept
	{
		// acquire appropriate locks on both containers
		write_lock_type  lock_this(this->mutex, std::defer_lock);
		write_lock_type  lock_that(other.mutex, std::defer_lock);
		std::scoped_lock lock(lock_this, lock_that);

		queue = std::move(other.queue);
		new_element.notify_all();
		return *this;
	}

	/// @}

	//----------------------------
	//  THREAD-SAFE PUBLIC METHODS
	//----------------------------

	/// @{
	/// @name Thread-safe Public Members
	/// @details These methods are safe for use in situations where the queue will be concurrently accessed by
	///          multiple threads.

	/// @brief Clears the concurrent queue, destroying any currently enqueued elements.
	/// @details This method is not concurrency-safe, and invalidates all iterators.
	inline void clear() noexcept
	{
		write_lock_type lock_this(this->mutex);
		queue.clear();
	}

	/// @brief Constructs a new element in place at the end of the concurrent queue.
	/// @details This method is concurrency-safe. This method is concurrency-safe with respect to calls to the
	///          methods `push`, `emplace`, `pop`, and `empty`.
	/// @tparam Args    Types of args, generally deduced automatically.
	/// @param[in] args  	Arguments to forward to the constructor of the element.
	template<class... Args>
	inline void emplace(Args&&... args)
	{
		write_lock_type lock_this(this->mutex);
		queue.emplace_back(std::forward<Args>(args)...);
		new_element.notify_one();
	}

	/// @brief Tests if the concurrent queue is empty at the moment this method is called.
	/// @details This method is concurrency-safe. While this method is concurrency-safe with
	///          respect to calls to the methods `push`, `emplace`, `pop`, and `empty`, the value returned
	///          might be incorrect by the time it is inspected by the calling thread.
	/// @return true if the concurrent queue was empty at the moment we looked, false otherwise.
	[[nodiscard]] inline bool empty() const noexcept
	{
		read_lock_type lock_this(this->mutex);
		return queue.empty();
	}

	/// @brief Returns a copy of the allocator used to construct the concurrent queue.
	/// @details This method is concurrency-safe.
	/// @return A copy of the allocator used to construct the concurrent queue.
	[[nodiscard]] inline allocator_type get_allocator() const noexcept
	{
		read_lock_type lock_this(this->mutex);
		return queue.get_allocator();
	}

	/// @brief Enqueues an item at tail end of the concurrent queue.
	/// @details This method is concurrency-safe. `push` is concurrency-safe with respect to calls to the methods `push`
	///          `emplace`, `pop`, and `empty`.
	/// @param[in] value The item to be added to the queue.
	inline void push(const T& value)
	{
		write_lock_type lock_this(this->mutex);
		queue.push_back(value);
		new_element.notify_one();
	}

	/// @brief Enqueues an item at tail end of the concurrent queue.
	/// @details This method is concurrency-safe. `push` is concurrency-safe with respect to calls to the methods `push`
	///          `emplace`, `pop`, and `empty`.
	/// @param[in] value The item to be added to the queue.
	inline void push(T&& value)
	{
		write_lock_type lock_this(this->mutex);
		queue.push_back(std::move(value));
		new_element.notify_one();
	}

	/// @brief Returns the number of items in the queue.
	/// @details This method is concurrency-safe. `push` is concurrency-safe with respect to calls to the methods `push`
	///          `emplace`, `try_pop`, and `empty`.
	/// @remarks While calls to size are concurrency-safe in that they cannot damage the internal state of the concurrent
	///          queue, it is unwise to use the results as a condition of a `for` loop or for iteration, because the
	///          size of the container could change between the call to `size` and the invocation of the loop's methods.
	/// @return The size of the concurrent queue.
	size_t size() const
	{
		read_lock_type lock_this(this->mutex);
		return queue.size();
	}

	/// @brief Dequeues an item from the queue if one is available.
	/// @details This method is concurrency-safe. If an item was successfully dequeued, the parameter `destination`
	///          receives the dequeued value, the original value held in the queue is destroyed, and this function
	///          returns true. If there was no item to dequeue, this function returns false without blocking, and
	///          the contents of the `destination` parameter are undefined. A false return value does not necessarily
	///          mean the queue is empty. `try_pop` is concurrency-safe with respect to calls to the methods `emplace`,
	///          `push`, `try_pop`, and `empty`.
	/// @param[out] destination A reference to a location to store the dequeued item.
	/// @return true if an item was successfully dequeued, false otherwise.
	inline bool try_pop(T& destination)
	{
		// In a perfect world, we would get a read lock, check whether the queue was empty, then upgrade that
		// to a write lock. However, there's no way to do that in C++17 (or 20). Should it ever become possible,
		// this block should be updated. For now, we'll just try to get write permissions from the onset and return
		// if we fail to get ownership of the lock to keep `pop` as snappy as possible.
		write_lock_type lock_this(this->mutex, std::defer_lock);

		// if it can't acquire the lock, or it did get the lock but the queue is empty, return false;
		if (!lock_this.try_lock() || queue.empty())
			return false;

		// we have the lock at this point. Move and pop the front of the queue
		destination = std::move(queue.front());
		queue.pop_front();
		return true;
	}

	/// @brief Dequeues an item from the queue if one is available within the specified timeout.
	/// @details This method is concurrency-safe. If an item was successfully dequeued, the parameter `destination`
	///          receives the dequeued value, the original value held in the queue is destroyed, and this function
	///          returns true. If there was no item to dequeue, this function returns false without blocking, and
	///          the contents of the `destination` parameter are undefined. A false return value does not necessarily
	///          mean the queue is empty. `try_pop` is concurrency-safe with respect to calls to the methods `emplace`,
	///          `push`, `try_pop`, and `empty`.
	/// @param[out] destination A reference to a location to store the dequeued item.
	/// @param[in] timeout_duration The maximum length of time `try_pop_for` will attempt to pop the queue before declaring
	///            failure.
	/// @return true if an item was successfully dequeued, false otherwise.
	template<class Rep, class Period>
	inline bool try_pop_for(T& destination, const std::chrono::duration<Rep, Period>& timeout_duration)
	{
		auto            start = std::chrono::steady_clock::now();
		write_lock_type lock_this(this->mutex, std::defer_lock);

		// return if we can't get the lock within the timeout period
		if (!lock_this.try_lock_for(timeout_duration))
			return false;

		// We have the lock now. If we haven't timed out, wait for the queue to have contents
		while (queue.empty())
		{
			auto elapsed = std::chrono::steady_clock::now() - start;
			if (elapsed > timeout_duration || new_element.wait_for(lock_this, timeout_duration - elapsed) == std::cv_status::timeout)
				return false;
		}

		// At this point the queue is not empty
		destination = std::move(queue.front());
		queue.pop_front();
		return true;
	}

	/// @}

	//----------------------------
	//  THREAD-UNSAFE ACCESSORS
	//----------------------------
	/// @{
	/// @name Thread-unsafe Public Members
	/// @details These methods are not recommended for use in production code and will generate warnings when used. That said, they're
	///			 helpful for testing and debug, which is why they are included.

	/// @brief Returns an iterator of type iterator to the beginning of the concurrent queue.
	/// @details This method is not concurrency-safe. The iterators for the concurrent_queue class are
	///          primarily intended for debugging, as they are slow, and iteration is not concurrency-safe
	///          with respect to other queue operations.
	/// @return An iterator of type iterator to the beginning of the concurrent queue.
#ifndef _CONCURRENT_QUEUE_NO_WARNINGS
	[[deprecated("concurrent_queue::begin() is not thread-safe. Define `_CONCURRENT_QUEUE_NO_WARNINGS` to disable this message")]]
#endif
	inline iterator
	begin()
	{
		return queue.begin();
	}

	/// @brief Returns an iterator of type const_iterator to the beginning of the concurrent queue.
	/// @details This method is not concurrency-safe. The iterators for the concurrent_queue class are
	///          primarily intended for debugging, as they are slow, and iteration is not concurrency-safe
	///          with respect to other queue operations.
	/// @return An iterator of type const_iterator to the beginning of the concurrent queue.
#ifndef _CONCURRENT_QUEUE_NO_WARNINGS
	[[deprecated("concurrent_queue::begin() is not thread-safe. Define `_CONCURRENT_QUEUE_NO_WARNINGS` to disable this message")]]
#endif
	inline const_iterator
	begin() const
	{
		return queue.cbegin();
	}

	/// @brief Returns an iterator of type const_iterator to the beginning of the concurrent queue.
	/// @details This method is not concurrency-safe. The iterators for the concurrent_queue class are
	///          primarily intended for debugging, as they are slow, and iteration is not concurrency-safe
	///          with respect to other queue operations.
	/// @return An iterator of type const_iterator to the beginning of the concurrent queue.
#ifndef _CONCURRENT_QUEUE_NO_WARNINGS
	[[deprecated("concurrent_queue::cbegin() is not thread-safe. Define `_CONCURRENT_QUEUE_NO_WARNINGS` to disable this message")]]
#endif
	inline const_iterator
	cbegin() const
	{
		return queue.cbegin();
	}

	/// @brief Returns an iterator of type iterator to the end of the concurrent queue.
	/// @details This method is not concurrency-safe. The iterators for the concurrent_queue class are
	///          primarily intended for debugging, as they are slow, and iteration is not concurrency-safe
	///          with respect to other queue operations.
	/// @return An iterator of type iterator to the end of the concurrent queue.
#ifndef _CONCURRENT_QUEUE_NO_WARNINGS
	[[deprecated("concurrent_queue::end() is not thread-safe. Define `_CONCURRENT_QUEUE_NO_WARNINGS` to disable this message")]]
#endif
	inline iterator
	end()
	{
		return queue.end();
	}

	/// @brief Returns an iterator of type const_iterator to the end of the concurrent queue.
	/// @details This method is not concurrency-safe. The iterators for the concurrent_queue class are
	///          primarily intended for debugging, as they are slow, and iteration is not concurrency-safe
	///          with respect to other queue operations.
	/// @return An iterator of type const_iterator to the end of the concurrent queue.
#ifndef _CONCURRENT_QUEUE_NO_WARNINGS
	[[deprecated("concurrent_queue::end() is not thread-safe. Define `_CONCURRENT_QUEUE_NO_WARNINGS` to disable this message")]]
#endif
	inline const_iterator
	end() const
	{
		return queue.cend();
	}

	/// @brief Returns an iterator of type const_iterator to the end of the concurrent queue.
	/// @details This method is not concurrency-safe. The iterators for the concurrent_queue class are
	///          primarily intended for debugging, as they are slow, and iteration is not concurrency-safe
	///          with respect to other queue operations.
	/// @return An iterator of type const_iterator to the end of the concurrent queue.
#ifndef _CONCURRENT_QUEUE_NO_WARNINGS
	[[deprecated("concurrent_queue::cend() is not thread-safe. Define `_CONCURRENT_QUEUE_NO_WARNINGS` to disable this message")]]
#endif
	inline const_iterator
	cend() const
	{
		return queue.cend();
	}

	/// @}

	//----------------------------
	//  [TEST USE] ACCESS CONTROL
	//----------------------------
	// These functions are primarily intended for testing/debugging, and are NOT the recommended interface for using
	// the concurrent queue (prefer `push` and `try_pop`). However, they may have some limited utility when iteration
	// is absolutely required.

	/// @{
	/// @name Access Control
	/// @details These methods provide the ability to lock the concurrent_queue for thread-safe iteration. They are
	///          primarily intended for test and debug use, and care must be taken when explicitely locking the queue to
	///			 avoid deadlock. These methods are NOT recommended for production code, and will produce warnings when used.

	/// @brief  Lock the queue in a manner in which it is safe for multiple threads to read-iterate over it
	/// @details Allows concurrency-safe iteration.
	/// @remarks Intended for test and debug use only.
	/// @return RAII lock suitable for safe read-iteration. Note that failure to move from or bind to the return value
	///         will invoke the locks destructor at the end of the call, instantly unlocking the concurrent queue.
#ifndef _CONCURRENT_QUEUE_NO_WARNINGS
	[[deprecated("concurrent_queue::acquire_read_lock() is not recommended for production code. Define `_CONCURRENT_QUEUE_NO_WARNINGS` to disable this message")]]
#endif
	[[nodiscard]] inline read_lock_type
	acquire_read_lock() const
	{
		return read_lock_type(this->mutex);
	}

	/// @brief  Lock the queue in a manner in which it is safe for multiple threads to write-iterate over it
	/// @details Allows concurrency-safe iteration.
	/// @remarks Intended for test and debug use only.
	/// @return RAII lock suitable for safe write-iteration. Note that failure to move from or bind to the return value
	///         will invoke the locks destructor at the end of the call, instantly unlocking the concurrent queue.
#ifndef _CONCURRENT_QUEUE_NO_WARNINGS
	[[deprecated("concurrent_queue::acquire_write_lock() is not recommended for production code. Define `_CONCURRENT_QUEUE_NO_WARNINGS` to disable this message")]]
#endif
	[[nodiscard]] inline write_lock_type
	acquire_write_lock()
	{
		return write_lock_type(this->mutex);
	}

	/// @}

	//----------------------------
	//  EQUALITY OPERATORS
	//----------------------------

	/// @{
	/// @name Equality Operators

	/// @brief Equality Operator
	/// @param rhs Right-hand side of the comparison
	/// @return true if the contents of the queues are equivalent, false otherwise.
	template<class Ty, template<class, class> class Q, class A>
	friend bool operator==(const concurrent_queue<Ty, Q, A>& lhs, const concurrent_queue<Ty, Q, A>& rhs);

	/// @brief Inequality Operator
	/// @param rhs Right-hand side of the comparison
	/// @return false if the contents of the queues are equivalent, true otherwise.
	template<class Ty, template<class, class> class Q, class A>
	friend bool operator!=(const concurrent_queue<Ty, Q, A>& lhs, const concurrent_queue<Ty, Q, A>& rhs);

	/// @}

	//----------------------------
	//  SWAP
	//----------------------------

	/// @{
	/// @name Swap Operator

	/// @brief Swap Operator
	/// @param lhs container to swap with rhs
	/// @param rhs container to swap with lhs
	template<class Ty, template<class, class> class Q, class A>
	friend void swap(concurrent_queue<Ty, Q, A>& lhs, concurrent_queue<Ty, Q, A>& rhs);

	/// @}

private:
	//----------------------------
	//  PRIVATE CONSTRUCTOR IMPLS
	//----------------------------

	/// @brief Copy Constructor Implementation
	/// @details by taking the lock as a parameter, it allows the queue to be copy-constructed, rather than copy-assigned
	/// @param[in] other container to copy
	/// @param[in] read_lock lock that has been locked on other.mutex
	inline concurrent_queue(const concurrent_queue& other, read_lock_type)
			: queue(other.queue)
	{
	}

	/// @brief Copy Constructor Implementation
	/// @details by taking the lock as a parameter, it allows the queue to be copy-constructed, rather than copy-assigned
	/// @param[in] other container to copy
	/// @param[in] alloc memory allocator
	/// @param[in] read_lock lock that has been locked on other.mutex
	inline concurrent_queue(const concurrent_queue& other, const allocator_type& alloc, read_lock_type)
			: queue(other.queue, alloc)
	{
	}

	/// @brief Move Constructor Implementation
	/// @details by taking the lock as a parameter, it allows the queue to be move-constructed, rather than move-assigned
	/// @param[in] other container to move
	/// @param[in] read_lock lock that has been locked on other.mutex
	inline concurrent_queue(concurrent_queue&& other, write_lock_type) noexcept
			: queue(std::move(other.queue))
	{
	}

	/// @brief move Constructor Implementation
	/// @details by taking the lock as a parameter, it allows the queue to be move-constructed, rather than move-assigned
	/// @param[in] other container to move
	/// @param[in] alloc memory allocator
	/// @param[in] read_lock lock that has been locked on other.mutex
	inline concurrent_queue(concurrent_queue&& other, const allocator_type& alloc, write_lock_type) noexcept
			: queue(std::move(other.queue), alloc)
	{
	}

private:
	//----------------------------
	//  PRIVATE MEMBERS
	//----------------------------
	// It's probably best to initialize
	// the mutex before the queue

	mutable mutex_type mutex;
	condition_type     new_element;
	queue_type         queue;
};

/// @brief Equality Operator
/// @return true if the queues are element-by-element equivalent
template<class T, template<class, class> class Queue = std::deque, class Alloc = std::allocator<T>>
inline bool operator==(const concurrent_queue<T, Queue, Alloc>& lhs, const concurrent_queue<T, Queue, Alloc>& rhs)
{
	typename concurrent_queue<T, Queue, Alloc>::read_lock_type lock_lhs(lhs.mutex, std::defer_lock);
	typename concurrent_queue<T, Queue, Alloc>::read_lock_type lock_rhs(rhs.mutex, std::defer_lock);
	std::scoped_lock                                           lock(lock_lhs, lock_rhs);

	if (lhs.queue.size() != rhs.queue.size())
		return false;

	for (int i = 0; i < lhs.queue.size(); ++i)
		if (lhs.queue[i] != rhs.queue[i])
			return false;

	return true;
}

/// @brief Inequality Operator
/// @return true if the queues are not element-by-element equivalent
template<class T, template<class, class> class Queue = std::deque, class Alloc = std::allocator<T>>
inline bool operator!=(const concurrent_queue<T, Queue, Alloc>& lhs, const concurrent_queue<T, Queue, Alloc>& rhs)
{
	return !(lhs == rhs);
}

namespace std
{
	/// @brief Swap Operator
	template<class T, template<class, class> class Queue = std::deque, class Alloc = std::allocator<T>>
	inline void swap(concurrent_queue<T, Queue, Alloc>& lhs, concurrent_queue<T, Queue, Alloc>& rhs)
	{
		if (&lhs != &rhs)
		{
			typename concurrent_queue<T, Queue, Alloc>::read_lock_type lock_lhs(lhs.mutex, std::defer_lock);
			typename concurrent_queue<T, Queue, Alloc>::read_lock_type lock_rhs(rhs.mutex, std::defer_lock);
			std::scoped_lock                                           lock(lock_lhs, lock_rhs);

			std::swap(lhs.queue, rhs.queue);
		}
	}
}    // namespace std

#endif    // concurrent_queue_h__