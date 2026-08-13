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
// Copyright (c) 2016 Nic Holthaus
//
//--------------------------------------------------------------------------------------------------
//
///	@file			stackTrace.h
///	@brief			Functor class which generates a stack trace from the point where it was called.
/// @details
//
//--------------------------------------------------------------------------------------------------

#ifndef stackTrace_h_
#define stackTrace_h_

//------------------------------
//	INCLUDES
//------------------------------

#include <string>

//	----------------------------------------------------------------------------
//	CLASS		stackTrace
//  ----------------------------------------------------------------------------
///	@brief
///	@details	This class is based in part on the work of Timo Bingmann located
///				here: https://panthema.net/2008/0901-stacktrace-demangled/
/// @note		If you use StackTrace in conjunction with exceptions, know that
///				you must construct the trace ***at the site of the throw***, NOT
///				in the `catch` statement, because the stack will already be
///				unwound at that point.
//  ----------------------------------------------------------------------------
class StackTrace
{
public:
	/**
	 * @brief		Constructor
	 * @param[in]	ignore				Number of items to ignore in the stack trace. Every time this class is
	 *									composed in another class or inherited, this number should be incremented
	 *									by `1`.
	 * @param[in]	deduplicateByStack	When true, the trace is suppressed (an EMPTY result) if the exact same
	 *									call stack has already been traced in this process; identical repeats do
	 *									not re-symbolize and are not re-logged, while a DIFFERENT path (a distinct
	 *									call stack) always produces a fresh full trace. When false (the default)
	 *									the trace is always produced in full, which is the correct behavior for a
	 *									crash dump or a thrown exception, neither of which may ever be suppressed.
	 *									The deduplication key is a hash of the raw return addresses captured BEFORE
	 *									symbolization, so the cheap stack capture is always paid but the expensive
	 *									per-frame symbol resolution is gated behind a first-seen-stack check.
	 */
	explicit StackTrace(unsigned int ignore = 0, bool deduplicateByStack = false);

	/**
	 * @brief		Stack Trace data
	 * @returns		The results of the stack trace as a string
	*/
	[[nodiscard]] const char* data() const noexcept;

	/**
	 * @brief		Whether this trace was suppressed as a duplicate stack (deduplicateByStack only).
	 * @returns		true if an identical call stack was already traced this process and this trace is therefore
	 *				empty; false if this trace carries content (a first-seen stack, or deduplication not requested).
	 */
	[[nodiscard]] bool suppressed() const noexcept;

	/**
	 * @brief		Stack trace string
	 * @returns		The results of the stack trace as a QString
	*/
	operator std::string() const;

	/**
	 * @brief		Forget every recorded stack, so the next occurrence of each call stack traces again.
	 * @details		For test isolation and for a caller that wants a fresh full-trace baseline (for example at
	 *				the start of a new run or session). Thread-safe.
	 */
	static void resetDeduplication() noexcept;

	/**
	 * @brief		Symbolize a caller-provided array of raw return addresses into the formatted trace footer.
	 * @details		Shares the exact per-frame symbolization and text formatting used by the constructor, so a
	 *				caller that captured its own frames (the async trace-log worker) produces identical output
	 *				without re-capturing. Serializes access to the process-global symbolizer (DbgHelp / BFD)
	 *				internally, so it is safe to call from any thread.
	 * @param[in]	frames	the raw return addresses to symbolize, in innermost-first order.
	 * @param[in]	count	the number of addresses in @p frames.
	 * @returns		The formatted, newline-terminated trace footer; empty when @p count is zero.
	 */
	[[nodiscard]] static std::string formatFrames(void* const* frames, int count);

	/**
	 * @brief		Whether the exact call stack in @p frames has already been traced in this process.
	 * @details		Records the stack on first sight so an identical repeat returns false thereafter. The key is
	 *				a hash of the raw return addresses, computed BEFORE any symbolization. Thread-safe. Used by
	 *				the async trace-log worker to run the same deduplication gate the constructor applies, but on
	 *				the worker thread against the caller-provided frames.
	 * @param[in]	frames	the raw return addresses that identify the stack.
	 * @param[in]	count	the number of addresses in @p frames.
	 * @returns		true the first time this exact stack is seen; false on every identical repeat.
	 */
	[[nodiscard]] static bool firstTimeForStack(void* const* frames, int count);

private:
	static const size_t MAX_FRAMES = 256;    ///< Arbitrary.

	std::string m_value;
	bool        m_suppressed = false;    ///< true when deduplicateByStack collapsed this trace as a repeat stack.
};
#endif    // stackTrace_h_
