#ifndef LIBLOGERR_LOGERRTHREAD_H
#define LIBLOGERR_LOGERRTHREAD_H

//----------------------------
//  INCLUDES
//----------------------------

#include <csignal>
#include <functional>
#include <logerrMacros.h>
#include <logerrTypes.h>
#include <mutex>
#include <tuple>
#include <thread>
#include <type_traits>
#include <utility>

namespace logerr
{
	//----------------------------------------------------------------------------------------------------------------------
	//      CLASS: LogerrThread
	//----------------------------------------------------------------------------------------------------------------------
	/// Thread class capable of catching exceptions
	//----------------------------------------------------------------------------------------------------------------------
	class thread : public std::jthread
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
	    : std::jthread()
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
	    : std::jthread(
	            [function = std::decay_t<Function>(std::forward<Function>(f)),
	             arguments = std::make_tuple(std::forward<Args>(args)...)](std::stop_token stop) mutable noexcept
	            {
		            try
		            {
			            std::apply(
			                [&](auto&&... unpacked)
			                {
				                if constexpr (std::is_invocable_v<decltype(function)&, std::stop_token, decltype(unpacked)...>)
					                std::invoke(function, stop, std::forward<decltype(unpacked)>(unpacked)...);
				                else
					                std::invoke(function, std::forward<decltype(unpacked)>(unpacked)...);
			                },
			                std::move(arguments));
		            }
		            catch (...)
		            {
			            logerr::captureException(std::current_exception());
		            }
	            })
	{
	}
}    // namespace logerr
#endif    //LIBLOGERR_LOGERRTHREAD_H
