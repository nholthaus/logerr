#ifndef LIBLOGERR_LOGERRTHREAD_H
#define LIBLOGERR_LOGERRTHREAD_H

//----------------------------
//  INCLUDES
//----------------------------

#include <thread>
#include <csignal>

extern std::exception_ptr g_exceptionPtr;

template<class T>
std::decay_t<T> decay_copy(T&& v)
{
	return std::forward<T>(v);
}

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: LogerrThread
//----------------------------------------------------------------------------------------------------------------------
/// Thread class capable of catching exceptions
//----------------------------------------------------------------------------------------------------------------------
class LogerrThread : public std::thread
{
public:
	LogerrThread() noexcept;

	template<class Function, class... Args>
	explicit LogerrThread(Function&& f, Args&&... args);
};

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: CONSTRUTOR [public]
//----------------------------------------------------------------------------------------------------------------------
/// @brief Default Constructor
//----------------------------------------------------------------------------------------------------------------------
LogerrThread::LogerrThread() noexcept
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
LogerrThread::LogerrThread(Function&& f, Args&&... args)
		: std::thread(
		[](auto&& f, auto&& args)
		{
		  try
		  {
			  std::invoke(decay_copy(std::forward<Function>(f)),
			              decay_copy(std::forward<Args>(args))...);
		  }
		  catch (...)
		  {
			  g_exceptionPtr = std::make_exception_ptr(std::current_exception());
		  }
		},
		std::forward<Function>(f),
		std::forward<Args>(args)...)
{
}

#endif    //LIBLOGERR_LOGERRTHREAD_H
