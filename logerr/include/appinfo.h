//--------------------------------------------------------------------------------------------------
//
///	@PROJECT	@APPLICATION_NAME@
/// @BRIEF		application information
///	@DETAILS
//
//--------------------------------------------------------------------------------------------------
//
// ATTRIBUTION:
// Parts of this work have been adapted from:
//
//--------------------------------------------------------------------------------------------------
//
// Copyright (c) 2020 Nic Holthaus. All Rights Reserved.
//
//--------------------------------------------------------------------------------------------------
//
//	XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
//	XXXX																						XXXX
//	XXXX						WARNING: THIS IS A GENERATED FILE								XXXX
//	XXXX						ABANDON ALL HOPE YE WHO EDIT HERE								XXXX
//	XXXX																						XXXX
//	XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
//
//--------------------------------------------------------------------------------------------------

#ifndef appinfo_h__
#define appinfo_h__

//----------------------------
//  INCLUDES
//----------------------------

#include <string>
#include <filesystem>

//----------------------------------------------------------------------------------------------------------------------
//      NAMESPACE: APPINFO
//----------------------------------------------------------------------------------------------------------------------
namespace APPINFO
{
	void setName(const std::string& name);
	void setName(const std::filesystem::path& path);

	// Application Info
	std::string name();
	std::string organization();
	std::string organizationDomain();
	std::string version();

	// Git Info
	std::string gitBranch();
	std::string gitCommitShort();
	std::string gitCommitLong();
	std::string gitTag();
	std::string gitDirty();
	std::string gitOrigin();
	std::string gitDirectory();
	std::string gitRepo();
	std::string gitUser();
	std::string gitEmail();

	// Build Host Info
	std::string buildHostname();
	std::string buildOSName();
	std::string buildOSVersion();
	std::string buildOSProcessor();
	std::string cmakeVersion();
	std::string compilerName();
	std::string compilerVersion();
	std::string qtVersion();

	// Runtime Host Info
	std::string hostCPUArchitecture();
	std::string hostKernelType();
	std::string hostKernelVersion();
	std::string hostName();
	std::string hostUniqueID();
	std::string hostPrettyProductName();
	std::string hostProductType();
	std::string hostProductVersion();

	// Path Info
	std::string home();
	std::string appDataDir();
	std::string logDir();
	std::string crashDumpDir();
	std::string documentsDir();
	std::string tempDir();
	std::string configDir();

	// Current Application Instance Info
	std::string applicationStartTime();

	// Full System Details
	std::string systemDetails();
}    // namespace APPINFO

#endif    // appinfo_h__
