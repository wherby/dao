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

#ifndef DAO_BASE_H
#define DAO_BASE_H

#include"dao.h"
#include"daoPlatforms.h"


#define IntToPointer( x ) ((void*)(size_t)(x))


typedef struct DRoutines     DRoutines;

typedef struct DaoCdataCore  DaoCdataCore;
typedef struct DaoTypeKernel DaoTypeKernel;
typedef struct DTypeSpecTree DTypeSpecTree;

typedef struct DaoToken      DaoToken;
typedef struct DaoInode      DaoInode;

typedef struct DaoVmCode     DaoVmCode;
typedef struct DaoVmCodeX    DaoVmCodeX;

typedef struct DVector   DVector;

typedef struct DaoFuture     DaoFuture;
typedef struct DaoException  DaoException;
typedef struct DaoNameValue  DaoNameValue;
typedef struct DaoConstant   DaoConstant;
typedef struct DaoVariable   DaoVariable;

typedef struct DaoCnode      DaoCnode;
typedef struct DaoOptimizer  DaoOptimizer;

typedef struct DaoMacro      DaoMacro;
typedef struct DaoParser     DaoParser;

typedef struct DaoStackFrame    DaoStackFrame;
typedef struct DaoDataCache     DaoDataCache;

#define STRCMP( x, y ) strcmp( (x)->mbs, y )
#define TOKCMP( x, y ) strcmp( (x)->string.mbs, y )

/*
// Bit structure of the lookup index:
// E2P2S4U8I16 = EEPPSSSSUUUUUUUUIIIIIIIIIIIIIIII
// E: Error; P: Permission; S: Storage; U: Up/parent; I: Index
*/
#define LOOKUP_BIND( st, pm, up, id )  (((pm)<<28)|((st)<<24)|((up)<<16)|id)

#define LOOKUP_BIND_LC( id ) ((DAO_LOCAL_CONSTANT<<24)|id)
#define LOOKUP_BIND_GC( id ) ((DAO_GLOBAL_CONSTANT<<24)|id)
#define LOOKUP_BIND_GV( id ) ((DAO_GLOBAL_VARIABLE<<24)|id)

#define LOOKUP_PM( one )  (((one)>>28)&3)
#define LOOKUP_ST( one )  (((one)>>24)&0xf)
#define LOOKUP_UP( one )  (((one)>>16)&0xff)
#define LOOKUP_ID( one )  ((unsigned short)((one)&0xffff))

#define LOOKUP_ISCST( one ) (LOOKUP_ST(one)&1)


typedef struct DaoConfig  DaoConfig;
struct DaoConfig
{
	short cpu;  /* number of CPU */
	short jit;  /* enable JIT compiling */
	short safe; /* enable safe running mode */
	short typedcode; /* enable typed VM codes */
	short optimize;  /* enable optimization */
	short iscgi;     /* is CGI script */
	short tabspace;  /* number of spaces counted for a tab */
	short chindent;  /* check indentation */
	short mbs; /* MBS only */
	short wcs; /* WCS only */
};

extern DaoConfig daoConfig;


#endif
