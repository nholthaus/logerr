// ---------------------------------------------------------------------------------------------------------------------
//
/// @file       sigtermHandler
/// @author     Nic Holthaus
/// @date       11/10/2020
/// @copyright  (c) 2020 Nic Holthaus
//
// ---------------------------------------------------------------------------------------------------------------------
//
/// @brief      $BRIEF
/// @details
//
// ---------------------------------------------------------------------------------------------------------------------

#include "sigtermHandler.h"

void sigtermHandler(int)
{
	throw TerminateException();
}