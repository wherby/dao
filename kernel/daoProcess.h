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

#ifndef DAO_PROCESS_H
#define DAO_PROCESS_H

#include"time.h"
#include"stdlib.h"

#include"daoVmcode.h"
#include"daoType.h"
#include"daoThread.h"
#include"daoOptimizer.h"

#define DVM_FRAME_RUNNING  (1<<0)
#define DVM_FRAME_FINISHED (1<<1)
#define DVM_FRAME_SECT     (1<<5)
#define DVM_FRAME_KEEP     (1<<6)

typedef struct DaoProcessAux DaoProcessAux;


struct DaoStackFrame
{
	int             entry;      /* entry code id; */
	ushort_t        state;      /* frame state; */
	ushort_t        returning;  /* return register id; */
	ushort_t        parCount;   /* the actual number of parameters passed in; */
	ushort_t        varCount;   /* the number of variables allocated on the stack; */
	daoint          stackBase;  /* the offset on the stack for the local variables; */
	daoint          deferBase;  /* the offset on the DaoProcess::defers list; */
	daoint          exceptBase; /* the offset on the DaoProcess::exceptions list; */

	DaoVmCode      *codes; /* = routine->vmCodes->codes; */
	DaoType       **types;
	DaoType        *retype;
	DaoRoutine     *routine;
	DaoObject      *object;
	DaoProcess     *outer;

	DaoStackFrame  *active;
	DaoStackFrame  *sect; /* original frame of a code section frame; */
	DaoStackFrame  *prev;
	DaoStackFrame  *next;
};

/*
   The stack structure of a Dao virtual machine process:

   1. The call/stack frames are organized into a linked list structure;

   2. The first frame is auxialiary frame that contains one stack value,
   which will be used to hold the returned value of the process (when the
   DVM_RETURN instruction is executed while the_current_frame->returning==-1);

   3. The stack values are stored in a dynamic array which can grow when
   a new frame is pushed into the stack;

   4. When the value stack grows, it must have extra space that can hold
   the maximum number of parameters (namely, @stackSize > @stackTop + DAO_MAX_PARAM);

   5. When a Dao function or C function is called, the parameters must be
   passed to the stack values starting from @stackTop, then a new frame can
   be push. In this way, it will avoid of the problem of invalidating some of
   the pointers when the stack is growed;

   6. After the value stack is expanded, the expanded part should be set to zero;
   the rest should be kept intact. The values from @stackTop to @stackSize can be
   collected when it is convenient, not each time when a frame is popped off.
 */

struct DaoProcess
{
	DAO_DATA_COMMON;

	DaoVmSpace     *vmSpace;

	DaoStackFrame  *firstFrame; /* the first frame */
	DaoStackFrame  *topFrame;   /* top call frame */

	DaoVmCode      *activeCode;
	DaoRoutine     *activeRoutine;
	DaoObject      *activeObject;
	DaoNamespace   *activeNamespace;
	DaoType       **activeTypes;
	DaoValue      **activeValues;
	DaoValue      **paramValues;
	DaoValue      **stackValues;
	daoint          stackSize; /* capacity of stackValues; */
	daoint          stackTop; /* one past the last active stack value; */

	uchar_t         parCount;
	uchar_t         pauseType;
	uchar_t         status;
	uchar_t         active : 4;
	uchar_t         stopit : 4;
	ushort_t        depth;

	DaoFuture      *future;
	DaoStream      *stdioStream;

	DArray         *defers;
	DArray         *exceptions;
	DArray         *factory;
	DaoDataCache   *cache;

#ifdef DAO_WITH_THREAD
	DMutex         *mutex; /* mutex for mt.critical::{} */
#endif

	DString        *mbstring;
	DaoProcessAux  *aux;
};

/* Create a new virtual machine process */
DAO_DLL DaoProcess* DaoProcess_New( DaoVmSpace *vms );
DAO_DLL void DaoProcess_Delete( DaoProcess *self );

DAO_DLL DaoStackFrame* DaoProcess_PushFrame( DaoProcess *self, int size );
DAO_DLL DaoStackFrame* DaoProcess_PushSectionFrame( DaoProcess *self );
DAO_DLL void DaoProcess_PopFrame( DaoProcess *self );
DAO_DLL void DaoProcess_PopFrames( DaoProcess *self, DaoStackFrame *rollback );

DAO_DLL void DaoProcess_InitTopFrame( DaoProcess *self, DaoRoutine *routine, DaoObject *object );
DAO_DLL void DaoProcess_SetActiveFrame( DaoProcess *self, DaoStackFrame *frame );

DAO_DLL void DaoProcess_PushRoutine( DaoProcess *self, DaoRoutine *routine, DaoObject *object );
DAO_DLL void DaoProcess_PushFunction( DaoProcess *self, DaoRoutine *function );
DAO_DLL int DaoProcess_PushCallable( DaoProcess *self, DaoRoutine *M, DaoValue *O, DaoValue *P[], int N );

DAO_DLL void DaoProcess_InterceptReturnValue( DaoProcess *self );

DAO_DLL void DaoProcess_MakeTuple( DaoProcess *self, DaoTuple *tuple, DaoValue *its[], int N );
DAO_DLL DaoRoutine* DaoProcess_PassParams( DaoProcess *self, DaoRoutine *routine, DaoType *hostype, DaoValue *obj, DaoValue *p[], int np, int code );

DAO_DLL int DaoProcess_Call( DaoProcess *self, DaoRoutine *f, DaoValue *o, DaoValue *p[], int n );

DAO_DLL void DaoProcess_CallFunction( DaoProcess *self, DaoRoutine *func, DaoValue *p[], int n );

/* Execute from the top of the calling stack */
DAO_DLL int DaoProcess_Execute( DaoProcess *self );

/* Acquire and release the condition variable: */
DAO_DLL void DaoProcess_AcquireCV( DaoProcess *self );
DAO_DLL void DaoProcess_ReleaseCV( DaoProcess *self );
DAO_DLL void DaoProcess_Suspend( DaoProcess *self, int type );

DAO_DLL int DaoProcess_PutReference( DaoProcess *self, DaoValue *refer );
DAO_DLL DaoValue* DaoProcess_SetValue( DaoProcess *self, ushort_t reg, DaoValue *value );

DAO_DLL void DaoProcess_PrintException( DaoProcess *self, DaoStream *stream, int clear );

DAO_DLL void DaoProcess_Trace( DaoProcess *self, int depth );

DAO_DLL DaoValue* DaoProcess_MakeConst( DaoProcess *self );



typedef struct DaoJIT         DaoJIT;
typedef struct DaoJitCallData DaoJitCallData;

typedef void (*DaoJIT_InitFPT)( DaoVmSpace*, DaoJIT* );
typedef void (*DaoJIT_QuitFPT)();
typedef void (*DaoJIT_FreeFPT)( void *jitdata );
typedef void (*DaoJIT_CompileFPT)( DaoRoutine *routine, DaoOptimizer *optimizer );
typedef void (*DaoJIT_ExecuteFPT)( DaoProcess *process, DaoJitCallData *data, int jitcode );

struct DaoJIT
{
	void (*Quit)();
	void (*Free)( void *jitdata );
	void (*Compile)( DaoRoutine *routine, DaoOptimizer *optimizer );
	void (*Execute)( DaoProcess *process, DaoJitCallData *data, int jitcode );
};

extern struct DaoJIT dao_jit;

struct DaoJitCallData
{
	DaoValue     **localValues;
	DaoValue     **localConsts;

	DaoValue     **objectValues;
	DaoVariable  **classValues;
	DaoConstant  **classConsts;

	DaoVariable  **globalValues;
	DaoConstant  **globalConsts;

	DaoProcess **processes;
};


#define DAO_MTCOUNT 624

struct DaoProcessAux
{
	DMap   *regexCaches; /* DHash<DString*,DString*> */
	uint_t  mtBuffer[DAO_MTCOUNT];
	int     mtIndex;
};
DAO_DLL DaoProcessAux* DaoProcessAux_New();
DAO_DLL void DaoProcessAux_Delete( DaoProcessAux *self );

/* Mersenne twister random number in [0,1] interval: */
DAO_DLL double DaoProcess_Random( DaoProcess *self );

#endif
