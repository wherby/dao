/*
// Dao Virtual Machine
// http://www.daovm.net
//
// Copyright (c) 2006-2013, Limin Fu
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED  BY THE COPYRIGHT HOLDERS AND  CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED  WARRANTIES,  INCLUDING,  BUT NOT LIMITED TO,  THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL  THE COPYRIGHT HOLDER OR CONTRIBUTORS  BE LIABLE FOR ANY DIRECT,
// INDIRECT,  INCIDENTAL, SPECIAL,  EXEMPLARY,  OR CONSEQUENTIAL  DAMAGES (INCLUDING,
// BUT NOT LIMITED TO,  PROCUREMENT OF  SUBSTITUTE  GOODS OR  SERVICES;  LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY OF
// LIABILITY,  WHETHER IN CONTRACT,  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DAO_VMSPACE_H
#define DAO_VMSPACE_H

#include"stdio.h"

#include"daoType.h"
#include"daoThread.h"
#include"daoOptimizer.h"
#include"daoProcess.h"

enum DaoPathType
{
	DAO_FILE_PATH,
	DAO_DIR_PATH
};

enum DaoModuleTypes
{
	DAO_MODULE_NONE,
	DAO_MODULE_DAC,
	DAO_MODULE_DAO,
	DAO_MODULE_DLL,
	DAO_MODULE_ANY
};

extern const char *const dao_copy_notice;

/* Dao Virtual Machine Space:
 * For handling:
 * -- Execution options and configuration;
 * -- Module loading and namespace management;
 * -- C types and functions defined in modules;
 * -- Path management;
 */
struct DaoVmSpace
{
	DAO_DATA_COMMON;

	/* To run the main script specified in the commad line (or the first loaded one),
	 * or scripts from an interactive console. */
	DaoProcess  *mainProcess;
	/* To store globals in the main script,
	 * or scripts from an interactive console. */
	DaoNamespace  *mainNamespace;

	/* for some internal scripts and predefined objects or types */
	DaoNamespace  *nsInternal;

	DaoStream  *stdioStream;
	DaoStream  *errorStream;

	DMap    *allProcesses;
	DMap    *allParsers;
	DMap    *allInferencers;
	DMap    *allOptimizers;
	DMap    *allProcessAux;

	DArray  *processes;
	DArray  *parsers;
	DArray  *inferencers;
	DArray  *optimizers;
	DArray  *processaux;

	DString *daoBinPath;
	DString *startPath;
	DString *mainSource;
	DString *pathWorking;
	DArray  *nameLoading;
	DArray  *pathLoading;
	DArray  *pathSearching; /* <DString*> */
	DArray  *virtualPaths;  /* <DString*> */

	DArray  *preloadModules;
	DArray  *loadedModules;
	DArray  *sourceArchive;

	int options;
	char stopit;
	char safeTag;
	char evalCmdline;
	char hasAuxlibPath;
	char hasSyslibPath;

	DMap  *vfiles;
	DMap  *vmodules;

	/* map full file name (including path and suffix) to module namespace */
	DMap  *nsModules; /* No GC for this, namespaces should remove themselves from this; */

	DaoUserHandler *userHandler;

	char* (*ReadLine)( const char *prompt );
	int   (*AddHistory)( const char *cmd );

#ifdef DAO_WITH_THREAD
	DMutex  mutexLoad;
	DMutex  mutexProc;
	DMutex  mutexMisc;
#endif
};

extern DaoVmSpace *mainVmSpace;

DAO_DLL DaoVmSpace* DaoVmSpace_New();
/* DaoVmSpace is not handled by GC, it should be deleted manually.
 * Normally, DaoVmSpace structures are allocated in the beginning of a program and
 * persist until the program exits. So DaoVmSpace_Delete() is rarely needed to be called.
 */
DAO_DLL void DaoVmSpace_Delete( DaoVmSpace *self );

DAO_DLL void DaoVmSpace_Lock( DaoVmSpace *self );
DAO_DLL void DaoVmSpace_Unlock( DaoVmSpace *self );

DAO_DLL int DaoVmSpace_ParseOptions( DaoVmSpace *self, const char *options );

DAO_DLL int DaoVmSpace_RunMain( DaoVmSpace *self, const char *file );

DAO_DLL DaoNamespace* DaoVmSpace_Load( DaoVmSpace *self, const char *file );
DAO_DLL DaoNamespace* DaoVmSpace_LoadEx( DaoVmSpace *self, const char *file, int run );

DAO_DLL DaoNamespace* DaoVmSpace_LoadModule( DaoVmSpace *self, DString *fname );
DAO_DLL DaoNamespace* DaoVmSpace_FindModule( DaoVmSpace *self, DString *fname );
DAO_DLL DaoNamespace* DaoVmSpace_FindNamespace( DaoVmSpace *self, DString *name );

DAO_DLL int DaoVmSpace_SearchResource( DaoVmSpace *self, DString *fname );

DAO_DLL void DaoVmSpace_SearchPath( DaoVmSpace *self, DString *fname, int type, int check );
DAO_DLL int DaoVmSpace_CompleteModuleName( DaoVmSpace *self, DString *fname );

DAO_DLL void DaoVmSpace_SetPath( DaoVmSpace *self, const char *path );
DAO_DLL void DaoVmSpace_AddPath( DaoVmSpace *self, const char *path );
DAO_DLL void DaoVmSpace_DelPath( DaoVmSpace *self, const char *path );

DAO_DLL DaoTypeBase* DaoVmSpace_GetTyper( short type );

DaoParser* DaoVmSpace_AcquireParser( DaoVmSpace *self );
DaoInferencer* DaoVmSpace_AcquireInferencer( DaoVmSpace *self );
DaoOptimizer* DaoVmSpace_AcquireOptimizer( DaoVmSpace *self );
DaoProcessAux* DaoVmSpace_AcquireProcessAux( DaoVmSpace *self );
void DaoVmSpace_ReleaseParser( DaoVmSpace *self, DaoParser *parser );
void DaoVmSpace_ReleaseInferencer( DaoVmSpace *self, DaoInferencer *inferencer );
void DaoVmSpace_ReleaseOptimizer( DaoVmSpace *self, DaoOptimizer *optimizer );
void DaoVmSpace_ReleaseProcessAux( DaoVmSpace *self, DaoProcessAux *processaux );

#endif
