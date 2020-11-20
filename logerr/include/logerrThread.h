#ifndef LIBLOGERR_LOGERRTHREAD_H
#define LIBLOGERR_LOGERRTHREAD_H

//----------------------------
//  INCLUDES
//----------------------------

#include <csignal>
#include <functional>
#include <logerrTypes.h>
#include <thread>

extern std::exception_ptr g_exceptionPtr;

template<class T>
std::decay_t<T> decay_copy(T&& v)
{
	return std::forward<T>(v);
}

namespace logerr
{
	//----------------------------------------------------------------------------------------------------------------------
	//      CLASS: LogerrThread
	//----------------------------------------------------------------------------------------------------------------------
	/// Thread class capable of catching exceptions
	//----------------------------------------------------------------------------------------------------------------------
	class thread : public std::thread
	{
	public:
		inline thread() noexcept;

		template<class Function, class... Args>
		inline explicit thread(Function&& f, Args&&... args);
	};

	//----------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: CONSTRUTOR [public]
	//----------------------------------------------------------------------------------------------------------------------
	/// @brief Default Constructor
	//----------------------------------------------------------------------------------------------------------------------
	thread::thread() noexcept
	    : std::thread()
	{
	}

	//----------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: CONSTRUCTOR [public]
	//----------------------------------------------------------------------------------------------------------------------
	/// @brief Construct from callable object
	/// @tparam Function Function type
	/// @tparam Args Argument type parameter pack
	/// @param f function to run in thread
	/// @param args arguments to the function
	//----------------------------------------------------------------------------------------------------------------------
	template<class Function, class... Args>
	thread::thread(Function&& f, Args&&... args)
	    : std::thread(
	            [](auto&& f, auto&&... args)
	            {
		            try
		            {
			            std::invoke(decay_copy(std::forward<Function>(f)),
			                        decay_copy(std::forward<Args>(args))...);
		            }
		            catch (const logerr::exception& e)
		            {
			            g_exceptionPtr = std::make_exception_ptr(e);
		            }
		            catch (const std::exception& e)
		            {
			            g_exceptionPtr = std::make_exception_ptr(e);
		            }
		            catch (...)
		            {
			            g_exceptionPtr = std::current_exception();
		            }
	            },
	            std::forward<Function>(f),
	            std::forward<Args>(args)...)
	{
	}
}    // namespace logerr
#endif    //LIBLOGERR_LOGERRTHREAD_H
