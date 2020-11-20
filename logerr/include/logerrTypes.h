#ifndef LIBLOGERR_LOGERRTYPES_H
#define LIBLOGERR_LOGERRTYPES_H

//----------------------------
//  INCLUDES
//----------------------------

#include <StackTraceException.h>
#include <logerrThread.h>
#include <sigtermHandler.h>
#include <timestampLite.h>

//----------------------------
//  TYPES
//----------------------------

namespace logerr
{
	using exception           = StackTraceException;
	using timestamp           = TimestampLite;
	using thread              = LogerrThread;
	using terminate_exception = TerminateException;
}    // namespace logerr

#endif    //LIBLOGERR_LOGERRTYPES_H
