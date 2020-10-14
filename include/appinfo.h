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

#include <QString>
#include <QDateTime>

namespace APPINFO
{
	// Application Info
	QString name();
	QString organization();
	QString organizationDomain();
	QString version();

	// Git Info
	QString gitBranch();
	QString gitCommitShort();
	QString gitCommitLong();
	QString gitTag();
	QString gitDirty();
	QString gitOrigin();
	QString gitDirectory();
	QString gitRepo();
	QString gitUser();
	QString gitEmail();

	// Build Host Info
	QString buildHostname();
	QString buildOSName();
	QString buildOSVersion();
	QString buildOSProcessor();
	QString cmakeVersion();
	QString compilerName();
	QString compilerVersion();
	QString qtVersion();

	// Runtime Host Info
	QString hostCPUArchitecture();
	QString hostKernelType();
	QString hostKernelVersion();
	QString hostName();
	QString hostUniqueID();
	QString hostPrettyProductName();
	QString hostProductType();
	QString hostProductVersion();

	// Path Info
	QString appDataDir();
	QString logDir();
	QString crashDumpDir();
	QString documentsDir();
	QString tempDir();
	QString configDir();

	// Current Application Instance Info
	QDateTime applicationStartTime();

	// Full System Details
	QString systemDetails();
}

#endif // appinfo_h__