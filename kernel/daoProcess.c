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

#include<stdio.h>
#include<string.h>
#include<assert.h>
#include<ctype.h>
#include<math.h>

#include"daoProcess.h"
#include"daoGC.h"
#include"daoStdlib.h"
#include"daoClass.h"
#include"daoObject.h"
#include"daoRoutine.h"
#include"daoVmspace.h"
#include"daoNamespace.h"
#include"daoNumtype.h"
#include"daoRegex.h"
#include"daoStream.h"
#include"daoParser.h"
#include"daoValue.h"
#include"daoTasklet.h"


extern DMutex mutex_routine_specialize;
extern DMutex mutex_routine_specialize2;

struct DaoJIT dao_jit = { NULL, NULL, NULL, NULL };


static DaoArray* DaoProcess_GetArray( DaoProcess *self, DaoVmCode *vmc );
static DaoList* DaoProcess_GetList( DaoProcess *self, DaoVmCode *vmc );
static DaoMap* DaoProcess_GetMap( DaoProcess *self, DaoVmCode *vmc, unsigned int hashing );

static void DaoProcess_DoMap( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoList( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoPair( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoTuple( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoVector( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoMatrix( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoAPList(  DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoAPVector( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoPacking( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoCheck( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_BindNameValue( DaoProcess *self, DaoVmCode *vmc );

static void DaoProcess_DoGetItem( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoSetItem( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoGetField( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoSetField( DaoProcess *self, DaoVmCode *vmc );

static void DaoProcess_DoIter( DaoProcess *self, DaoVmCode *vmc );

static void DaoProcess_DoInTest( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoBinArith( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoBinBool(  DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoUnaArith( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoBitLogic( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoBitShift( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoBitFlip( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoBitFlip( DaoProcess *self, DaoVmCode *vmc );

static void DaoProcess_DoCast( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_DoCall( DaoProcess *self, DaoVmCode *vmc );

/* if return TRUE, there is exception, and look for the next rescue point. */
static int DaoProcess_DoCheckExcept( DaoProcess *self, DaoVmCode *vmc );
/* if return DAO_STATUS_EXCEPTION, real exception is rose, and look for the next rescue point. */
static void DaoProcess_DoRaiseExcept( DaoProcess *self, DaoVmCode *vmc );
/* return TRUE, if some exceptions can be rescued */
static int DaoProcess_DoRescueExcept( DaoProcess *self, DaoVmCode *vmc );
static void DaoProcess_RaiseTypeError( DaoProcess *self, DaoType *from, DaoType *to, const char *op );

static void DaoProcess_MakeRoutine( DaoProcess *self, DaoVmCode *vmc );

static DaoVmCode* DaoProcess_DoSwitch( DaoProcess *self, DaoVmCode *vmc );
static DaoValue* DaoProcess_DoReturn( DaoProcess *self, DaoVmCode *vmc );
static int DaoVM_DoMath( DaoProcess *self, DaoVmCode *vmc, DaoValue *C, DaoValue *A );

int DaoArray_number_op_array( DaoArray *C, DaoValue *A, DaoArray *B, short op, DaoProcess *ctx );
int DaoArray_array_op_number( DaoArray *C, DaoArray *A, DaoValue *B, short op, DaoProcess *ctx );
int DaoArray_ArrayArith( DaoArray *s, DaoArray *l, DaoArray *r, short p, DaoProcess *c );
void DaoProcess_ShowCallError( DaoProcess *self, DaoRoutine *rout, DaoValue *selfobj, DaoValue *ps[], int np, int codemode );


static DaoStackFrame* DaoStackFrame_New()
{
	DaoStackFrame *self = (DaoStackFrame*) dao_calloc( 1, sizeof(DaoStackFrame) );
	return self;
}
#define DaoStackFrame_Delete( p ) dao_free( p )

DaoTypeBase vmpTyper =
{
	"process",
	& baseCore, NULL, NULL, {0}, {0},
	(FuncPtrDel) DaoProcess_Delete, NULL
};

static DaoType  *dummyType = NULL;
static DaoVmCode dummyCode = {0,0,0,0};

DaoProcess* DaoProcess_New( DaoVmSpace *vms )
{
	int i;
	DaoProcess *self = (DaoProcess*)dao_calloc( 1, sizeof( DaoProcess ) );
	DaoValue_Init( self, DAO_PROCESS );
	self->trait |= DAO_VALUE_DELAYGC;
	self->vmSpace = vms;
	self->status = DAO_PROCESS_SUSPENDED;
	self->exceptions = DArray_New(D_VALUE);
	self->defers = DArray_New(D_VALUE);

	self->firstFrame = self->topFrame = DaoStackFrame_New();
	self->firstFrame->active = self->firstFrame;
	self->firstFrame->types = & dummyType;
	self->firstFrame->codes = & dummyCode;
	self->firstFrame->entry = 1;
	self->stackSize = self->stackTop = 1 + DAO_MAX_PARAM;
	self->stackValues = (DaoValue**)dao_calloc( self->stackSize, sizeof(DaoValue*) );
	self->paramValues = self->stackValues + 1;
	self->factory = DArray_New(D_VALUE);

	self->mbstring = DString_New(1);
	self->pauseType = 0;
	self->active = 0;
	return self;
}

void DaoProcess_Delete( DaoProcess *self )
{
	DaoStackFrame *frame = self->firstFrame;
	daoint i;
	while( frame ){
		DaoStackFrame *p = frame;
		if( frame->object ) GC_DecRC( frame->object );
		if( frame->routine ) GC_DecRC( frame->routine );
		frame = frame->next;
		dao_free( p );
	}
	for(i=0; i<self->stackSize; i++) GC_DecRC( self->stackValues[i] );
	if( self->stackValues ) dao_free( self->stackValues );
	if( self->aux ) DaoVmSpace_ReleaseProcessAux( self->vmSpace, self->aux );
	DaoDataCache_Release( self->cache );
	self->cache = NULL;

	DString_Delete( self->mbstring );
	DArray_Delete( self->exceptions );
	DArray_Delete( self->defers );
	if( self->future ) GC_DecRC( self->future );
	if( self->factory ) DArray_Delete( self->factory );
	dao_free( self );
}


DaoStackFrame* DaoProcess_PushFrame( DaoProcess *self, int size )
{
	daoint i, N = self->stackTop + size;
	DaoStackFrame *f, *frame = self->topFrame->next;
	if( N > self->stackSize ){
		daoint offset = self->activeValues - self->stackValues;
		self->stackValues = (DaoValue**)dao_realloc( self->stackValues, N*sizeof(DaoValue*) );
		self->paramValues = self->stackValues + 1;
		memset( self->stackValues + self->stackSize, 0, (N-self->stackSize)*sizeof(DaoValue*) );
		if( self->activeValues ) self->activeValues = self->stackValues +  offset;
		self->stackSize = N;
	}
	if( frame == NULL ){
		frame = DaoStackFrame_New();
		self->topFrame->next = frame;
		frame->prev = self->topFrame;
	}

	/*
	// Each stack frame uses ::varCount number of local variables that are allocated
	// on the stack starting from ::stackBase. DaoProcess_InitTopFrame() may check
	// if the routine to be called is the same as the previous one called on this
	// frame, if yes, it will assume these variables initialized and used by the
	// previous call can be reused without re-initialization.
	//
	// Here it checks if the frame has the right stack offset and variable count,
	// if no, unset ::routine to force DaoProcess_InitTopFrame() redo the
	// initialization.
	//
	// A frame that is invalidated by previous frames will have its ::varCount set
	// to zero, so that this checking will always be sucessful (if size!=0).
	*/
	if( frame->routine && (frame->stackBase != self->stackTop || frame->varCount != size) ){
		GC_DecRC( frame->routine );
		frame->routine = NULL;
	}
	frame->sect = NULL;
	frame->stackBase = self->stackTop;
	frame->varCount = size;
	frame->entry = 0;
	frame->state = 0;
	frame->returning = -1;
	frame->deferBase = self->defers->size;
	frame->exceptBase = self->exceptions->size;
	if( self->topFrame->routine && self->topFrame->routine->body && self->activeCode ){
		self->topFrame->entry = (int)(self->activeCode - self->topFrame->codes) + 1;
		frame->returning = self->activeCode->c;
	}
	self->topFrame = frame;
	self->stackTop += size;

	/*
	// Check and reset frames that have the stack values invalidated for reusing.
	// A frame is invalidated if the range of its stack values is partially covered
	// by this frame.
	*/
	f = frame->next;
	while( f && f->stackBase < self->stackTop ){
		f->stackBase = self->stackTop;
		f->varCount = 0; /* To make sure this frame is re-initialized; */
		f = f->next;
	}
	return frame;
}
void DaoProcess_PopFrame( DaoProcess *self )
{
	int att = 0;
	if( self->topFrame == NULL ) return;
	if( self->topFrame->routine ){
		att = self->topFrame->routine->attribs;
		if( !(self->topFrame->routine->attribs & DAO_ROUT_REUSABLE) ){
			GC_DecRC( self->topFrame->routine );
			self->topFrame->routine = NULL;
		}
	}
	self->topFrame->outer = NULL;
	GC_DecRC( self->topFrame->retype );
	GC_DecRC( self->topFrame->object );
	self->topFrame->retype = NULL;
	self->topFrame->object = NULL;
	if( self->topFrame->state & DVM_FRAME_SECT ){
		self->topFrame = self->topFrame->prev;
		return;
	}
	if( att & DAO_ROUT_DEFERRED ) DArray_PopBack( self->defers );
	self->status = DAO_PROCESS_RUNNING;
	self->stackTop = self->topFrame->stackBase;
	self->topFrame = self->topFrame->prev;
	if( self->topFrame ) DaoProcess_SetActiveFrame( self, self->topFrame->active );
}
void DaoProcess_PopFrames( DaoProcess *self, DaoStackFrame *rollback )
{
	while( self->topFrame != rollback ) DaoProcess_PopFrame( self );
}
void DaoProcess_InitTopFrame( DaoProcess *self, DaoRoutine *routine, DaoObject *object )
{
	DaoStackFrame *frame = self->topFrame;
	DaoRoutineBody *body = routine->body;
	DaoValue **values = self->stackValues + frame->stackBase;
	DaoType **types = body->regType->items.pType;
	daoint *id = body->simpleVariables->items.pInt;
	daoint *end = id + body->simpleVariables->size;

	if( routine == frame->routine ) return;
	GC_ShiftRC( routine, frame->routine );
	frame->routine = routine;
	frame->codes = body->vmCodes->data.codes;
	frame->types = types;
	for(; id != end; id++){
		daoint i = *id, tid = types[i]->tid;
		DaoValue *value = values[i], *value2;
		if( value && value->type == tid && value->xGC.refCount == 1 && value->xGC.trait == 0 ) continue;
		value2 = NULL;
		switch( tid ){
		case DAO_NONE :
			value2 = dao_none_value;
			break;
		case DAO_INTEGER :  case DAO_FLOAT :  case DAO_DOUBLE : 
		case DAO_COMPLEX :  case DAO_LONG  :  case DAO_STRING :
			value2 = DaoDataCache_MakeValue( self->cache, tid );
			break;
		case DAO_ENUM :
			value2 = (DaoValue*) DaoDataCache_MakeEnum( self->cache, types[i] );
			break;
		}
		if( value2 == NULL ) continue;
		GC_ShiftRC( value2, value );
		values[i] = value2;
	}
}
void DaoProcess_SetActiveFrame( DaoProcess *self, DaoStackFrame *frame )
{
	frame = frame->active;
	self->activeObject = frame->object;
	self->activeCode = frame->codes + frame->entry - 1;
	self->activeValues = self->stackValues + frame->stackBase;
	self->activeTypes = frame->types;
	self->activeRoutine = frame->routine;
	if( frame->routine ) self->activeNamespace = frame->routine->nameSpace;
}
static void DaoProcess_CopyStackParams( DaoProcess *self )
{
	DaoValue **frameValues = self->stackValues + self->topFrame->stackBase;
	uchar_t i, defCount = self->topFrame->routine->parCount;
	self->topFrame->parCount = self->parCount;
	for(i=0; i<defCount; ++i){
		DaoValue *value = self->paramValues[i];
		if( value == NULL ) break;
		self->paramValues[i] = frameValues[i];
		frameValues[i] = value;
	}
}
void DaoProcess_PushRoutine( DaoProcess *self, DaoRoutine *routine, DaoObject *object )
{
	DaoType *routHost = routine->routHost;
	DaoStackFrame *frame = DaoProcess_PushFrame( self, routine->body->regCount );

	DaoProcess_InitTopFrame( self, routine, object );
	frame->active = frame;
	self->status = DAO_PROCESS_STACKED;
	DaoProcess_CopyStackParams( self );
	if( routHost && routHost->tid == DAO_OBJECT && !(routine->attribs & DAO_ROUT_STATIC) ){
		DaoValue *firstParam = frame->parCount ? self->paramValues[0] : NULL;
		if( firstParam && firstParam->type != DAO_OBJECT ) firstParam = NULL;
		if( object == NULL && firstParam != NULL ) object = (DaoObject*)firstParam;
		if( object ) object = (DaoObject*)DaoObject_CastToBase( object->rootObject, routHost );
#if 0
		printf( "%s %s\n", routine->routName->mbs, routine->routType->name->mbs );
		printf( "%s\n", routHost->name->mbs );
#endif
#ifdef DEBUG
		assert( object && object != (DaoObject*)object->defClass->objType->value );
#endif
		GC_ShiftRC( object, frame->object );
		frame->object = object;
	}
}
void DaoProcess_PushFunction( DaoProcess *self, DaoRoutine *routine )
{
	DaoStackFrame *frame = DaoProcess_PushFrame( self, routine->parCount );
	frame->active = frame->prev->active;
	GC_ShiftRC( routine, frame->routine );
	frame->routine = routine;
	self->status = DAO_PROCESS_STACKED;
	DaoProcess_CopyStackParams( self );
}
static int DaoRoutine_PassDefault( DaoRoutine *routine, DaoValue *dest[], int passed, DMap *defs, DaoDataCache *cache )
{
	DaoType *tp, *routype = routine->routType;
	DaoType **types = routype->nested->items.pType;
	DaoValue **consts = routine->routConsts->items.items.pValue;
	int i, ndef = routine->parCount;
	for(i=0; i<ndef; i++){
		int m = types[i]->tid;
		if( m == DAO_PAR_VALIST ) break;
		if( passed & (1<<i) ) continue;
		if( m != DAO_PAR_DEFAULT ) return 0;
		tp = & types[i]->aux->xType;
		if( DaoValue_Move2( consts[i], & dest[i], tp, defs, cache ) == 0 ) return 0;
		if( defs && (tp->tid == DAO_UDT || tp->tid == DAO_THT) ){
			DaoType *type = DaoNamespace_GetType( routine->nameSpace, consts[i] );
			if( !(type->attrib & DAO_TYPE_SPEC) ){
				if( DMap_Find( defs, tp ) == NULL ) DMap_Insert( defs, tp, type );
			}
		}
	}
	return 1;
}
/* Return the routine or its specialized version on success, and NULL on failure: */
DaoRoutine* DaoProcess_PassParams( DaoProcess *self, DaoRoutine *routine, DaoType *hostype, DaoValue *obj, DaoValue *p[], int np, int code )
{
	DMap *defs = NULL;
	DaoType *routype = routine->routType;
	DaoType *tp, **types = routype->nested->items.pType;
	DaoValue **dest = self->paramValues;
	size_t passed = 0;
	int mcall = code == DVM_MCALL;
	int need_self = routype->attrib & DAO_TYPE_SELF;
	int need_spec = routype->attrib & DAO_TYPE_SPEC;
	int ndef = routine->parCount;
	int npar = np;
	int ifrom, ito;
	int selfChecked = 0;
#if 0
	int i;
	printf( "%s: %i %i %i\n", routine->routName->mbs, ndef, np, obj ? obj->type : 0 );
	for(i=0; i<npar; i++){
		tp = DaoNamespace_GetType( routine->nameSpace, p[i] );
		printf( "%i  %s\n", i, tp->name->mbs );
	}
#endif

	self->parCount = 0;
	if( need_spec ){
		defs = DHash_New(0,0);
		if( hostype && routine->routHost && (routine->routHost->attrib & DAO_TYPE_SPEC) ){
			//XXX printf( "%s %s\n", hostype->name->mbs, routine->routHost->name->mbs );
			/* Init type specialization mapping for static methods: */
			DaoType_MatchTo( hostype, routine->routHost, defs );
		}
	}
	/* Check for explicit self parameter: */
	if( np && p[0]->type == DAO_PAR_NAMED ){
		DaoNameValue *nameva = & p[0]->xNameValue;
		if( nameva->unitype->attrib & DAO_TYPE_SELFNAMED ){
			obj = NULL;
			mcall = 1;
		}
	}

	if( mcall && ! need_self ){
		npar --;
		p ++;
	}else if( obj && need_self && ! mcall ){
		/* class DaoClass : CppClass{ cppmethod(); } */
		tp = & types[0]->aux->xType;
		if( obj->type < DAO_ARRAY ){
			if( tp == NULL || DaoType_MatchValue( tp, obj, defs ) == DAO_MT_EQ ){
				GC_ShiftRC( obj, dest[0] );
				dest[0] = obj;
				selfChecked = 1;
				passed = 1;
			}
		}else{
			if( obj->type == DAO_OBJECT && (tp->tid ==DAO_OBJECT || tp->tid ==DAO_CDATA || tp->tid ==DAO_CSTRUCT) ){
				/* calling C function on Dao object: */
				obj = DaoObject_CastToBase( (DaoObject*) obj, tp );
			}
			if( DaoValue_Move2( obj, & dest[0], tp, defs, self->cache ) ){
				selfChecked = 1;
				passed = 1;
				if( defs && (tp->tid == DAO_UDT || tp->tid == DAO_THT) ){
					DaoType *type = DaoNamespace_GetType( routine->nameSpace, obj );
					if( !(type->attrib & DAO_TYPE_SPEC) ){
						if( DMap_Find( defs, tp ) == NULL ) DMap_Insert( defs, tp, type );
					}
				}
			}
		}
	}
	/*
	   printf( "%s, rout = %s; ndef = %i; npar = %i, %i\n", routine->routName->mbs, routine->routType->name->mbs, ndef, npar, selfChecked );
	 */
	if( npar > ndef ) goto ReturnZero;
	if( (npar|ndef) ==0 ){
		if( defs ) DMap_Delete( defs );
		return routine;
	}
	/* pass from p[ifrom] to dest[ito], with type checking by types[ito] */
	for(ifrom=0; ifrom<npar; ifrom++){
		DaoValue *val = p[ifrom];
		ito = ifrom + selfChecked;
		if( ito < ndef && types[ito]->tid == DAO_PAR_VALIST ){
			tp = types[ito]->aux ? (DaoType*) types[ito]->aux : dao_type_any;
			for(; ifrom<npar; ifrom++){
				ito = ifrom + selfChecked;
				if( DaoValue_Move2( p[ifrom], & dest[ito], tp, defs, self->cache ) == 0 ) goto ReturnZero;
				passed |= (size_t)1<<ito;
			}
			break;
		}
		if( val->type == DAO_PAR_NAMED ){
			DaoNameValue *nameva = & val->xNameValue;
			DNode *node = DMap_Find( routype->mapNames, nameva->name );
			val = nameva->value;
			if( node == NULL ) goto ReturnZero;
			ito = node->value.pInt;
		}
		if( ito >= ndef ) goto ReturnZero;
		passed |= (size_t)1<<ito;
		tp = & types[ito]->aux->xType;
		if( need_self && ito ==0 ){
			if( val->type == DAO_OBJECT && (tp->tid ==DAO_OBJECT || tp->tid ==DAO_CDATA || tp->tid == DAO_CSTRUCT) ){
				/* for virtual method call */
				val = (DaoValue*) DaoObject_CastToBase( val->xObject.rootObject, tp );
				if( val == NULL ) goto ReturnZero;
			}else if( DaoType_MatchValue( tp, val, defs ) == DAO_MT_EQ ){
				GC_ShiftRC( val, dest[ito] );
				dest[ito] = val;
				continue;
			}
		}
		if( DaoValue_Move2( val, & dest[ito], tp, defs, self->cache ) == 0 ) goto ReturnZero;
		if( defs && (tp->tid == DAO_UDT || tp->tid == DAO_THT) ){
			DaoType *type = DaoNamespace_GetType( routine->nameSpace, val );
			if( !(type->attrib & DAO_TYPE_SPEC) ){
				if( DMap_Find( defs, tp ) == NULL ) DMap_Insert( defs, tp, type );
			}
		}
	}
	if( (selfChecked + npar) < ndef ){
		if( DaoRoutine_PassDefault( routine, dest, passed, defs, self->cache ) == 0 ) goto ReturnZero;
	}
	if( defs && defs->size ){ /* Need specialization */
		DaoRoutine *original = routine->original ? routine->original : routine;
		DaoRoutine *current = routine;
		/* Do not share function body. It may be thread unsafe to share: */
		routine = DaoRoutine_Copy( original, 0, 1, 0 );
		DaoRoutine_Finalize( routine, routine->routHost, defs );

		if( routine->routType->attrib & DAO_TYPE_SPEC ){
			DaoGC_TryDelete( (DaoValue*) routine );
			routine = current;
		}else{
			if( routine->body ){
				DMap *defs2 = DHash_New(0,0);
				DaoType_MatchTo( routine->routType, original->routType, defs2 );
				/* Only specialize explicitly declared variables: */
				DaoRoutine_MapTypes( routine, defs2 );
				DMap_Delete( defs2 );
				if( DaoRoutine_DoTypeInference( routine, 1 ) == 0 ){
					/*
					// Specialization may fail at unreachable parts for certain parameters.
					// Example: binary tree benchmark using list (binary_tree2.dao).
					// But DO NOT revert back to the original function body,
					// to avoid repeatly invoking of this specialization!
					 */
				}
			}
			DMutex_Lock( & mutex_routine_specialize );
			if( original->specialized == NULL ) original->specialized = DRoutines_New();
			DMutex_Unlock( & mutex_routine_specialize );

			GC_ShiftRC( original, routine->original );
			routine->original = original;
			DRoutines_Add( original->specialized, routine );
		}
	}
	if( defs ) DMap_Delete( defs );
	self->parCount = npar + selfChecked;
	return routine;
ReturnZero:
	if( defs ) DMap_Delete( defs );
	return NULL;
}
/* If the callable is a constructor, and O is a derived type of the constructor's type,
 * cast O to the constructor's type and then call the constructor on the casted object: */
int DaoProcess_PushCallable( DaoProcess *self, DaoRoutine *R, DaoValue *O, DaoValue *P[], int N )
{
	if( R == NULL ) return DAO_ERROR;
	R = DaoRoutine_ResolveX( R, O, P, N, DVM_CALL );
	if( R ) R = DaoProcess_PassParams( self, R, NULL, O, P, N, DVM_CALL );
	if( R == NULL ) return DAO_ERROR_PARAM;

	if( R->body ){
		int need_self = R->routType->attrib & DAO_TYPE_SELF;
		if( need_self && R->routHost && R->routHost->tid == DAO_OBJECT ){
			if( O == NULL && P[0]->type == DAO_OBJECT ) O = P[0];
			if( O ) O = DaoObject_CastToBase( O->xObject.rootObject, R->routHost );
			if( O == NULL || O == O->xObject.defClass->objType->value ) return DAO_ERROR;
		}
		DaoProcess_PushRoutine( self, R, DaoValue_CastObject( O ) );
	}else{
		DaoProcess_PushFunction( self, R );
	}
	return 0;
}
void DaoProcess_InterceptReturnValue( DaoProcess *self )
{
	if( self->topFrame->routine && self->topFrame->routine->body ){
		self->topFrame->returning = -1;
	}else{
		self->topFrame->active = self->firstFrame;
		DaoProcess_SetActiveFrame( self, self->firstFrame );
	}
}
int DaoProcess_Resume( DaoProcess *self, DaoValue *par[], int N, DaoProcess *ret )
{
	DaoType *tp;
	DaoVmCode *vmc;
	DaoTuple *tuple;
	if( self->status != DAO_PROCESS_SUSPENDED ) return 0;
	if( self->activeCode && self->activeCode->code == DVM_YIELD ){
		tp = self->activeTypes[ self->activeCode->c ];
		if( N == 1 ){
			DaoProcess_PutValue( self, par[0] );
		}else if( N ){
			tuple = DaoTuple_New( N );
			tuple->unitype = tp;
			GC_IncRC( tuple->unitype );
			DaoProcess_MakeTuple( self, tuple, par, N );
			DaoProcess_PutValue( self, (DaoValue*) tuple );
		}
		self->topFrame->entry ++;
	}else if( N ){
		DaoRoutine *rout = self->topFrame->routine;
		self->paramValues = self->stackValues + self->topFrame->stackBase;
		if( rout ) rout = DaoProcess_PassParams( self, rout, NULL, NULL, par, N, DVM_CALL );
		self->paramValues = self->stackValues + 1;
		if( rout == NULL ){
			DaoProcess_RaiseException( ret, DAO_ERROR, "invalid parameters." );
			return 0;
		}
	}
	DaoProcess_Execute( self );
	DaoProcess_PutValue( ret, self->stackValues[0] );
	return 1;
}

static DaoStackFrame* DaoProcess_FindSectionFrame( DaoProcess *self )
{
	DaoStackFrame *frame = self->topFrame;
	DaoType *cbtype = NULL;
	DaoVmCode *codes;
	int nop = 0;
	if( self->activeCode->code == DVM_EVAL ){
		codes = self->activeCode + 1 + (self->activeCode->b == 2);
		nop = codes[1].code == DVM_NOP;
		if( codes[nop].code == DVM_GOTO && codes[nop+1].code == DVM_SECT ) return frame;
		return NULL;
	}
	if( frame->routine ) cbtype = frame->routine->routType->cbtype;
	if( cbtype == NULL ) return NULL;
	if( frame->sect ){
		/* yield inside code section should execute code section for the routine: */
		frame = frame->sect->prev;
	}else{
		frame = frame->active;
	}
	while( frame != self->firstFrame ){
		DaoType *cbtype2 = NULL;
		if( frame->routine ){
			cbtype2 = frame->routine->routType->cbtype;
			if( frame->routine->body ){
				codes = frame->codes + frame->entry;
				nop = codes[1].code == DVM_NOP;
				if( codes[nop].code == DVM_GOTO && codes[nop+1].code == DVM_SECT ) return frame;
			}
		}
		if( cbtype2 == NULL || DaoType_MatchTo( cbtype, cbtype2, NULL ) == 0 ) break;
		frame = frame->prev;
	}
	if( frame == NULL || frame->routine == NULL ) return NULL;
	codes = frame->codes + frame->entry;
	nop = codes[1].code == DVM_NOP;
	if( codes[nop].code == DVM_GOTO && codes[nop+1].code == DVM_SECT ) return frame;
	return NULL;
}
DaoStackFrame* DaoProcess_PushSectionFrame( DaoProcess *self )
{
	DaoStackFrame *next, *frame = DaoProcess_FindSectionFrame( self );
	int asserting = self->activeCode->code == DVM_EVAL && self->activeCode->b == 2;
	int returning = -1;

	if( self->depth >= 1000 ){
		DaoProcess_RaiseException( self, DAO_ERROR, "Too deep nested code section method calls!" );
		return NULL;
	}
	if( frame == NULL ) return NULL;
	if( self->topFrame->routine->body ){
		self->topFrame->entry = 1 + self->activeCode - self->topFrame->codes;
		returning = self->activeCode->c;
	}
	next = DaoProcess_PushFrame( self, 0 );
	next->entry = frame->entry + 2 + asserting;
	next->state = DVM_FRAME_SECT | DVM_FRAME_KEEP;

	GC_ShiftRC( frame->object, next->object );
	GC_ShiftRC( frame->routine, next->routine );
	next->routine = frame->routine;
	next->object = frame->object;
	next->parCount = frame->parCount;
	next->stackBase = frame->stackBase;
	next->types = frame->types;
	next->codes = frame->codes;
	next->active = next;
	next->sect = frame;
	next->outer = self;
	next->returning = returning;
	DaoProcess_SetActiveFrame( self, frame );
	return frame;
}
static void DaoProcess_FlushStdStreams( DaoProcess *self )
{
	if( self->stdioStream ) DaoStream_Flush( self->stdioStream );
	DaoStream_Flush( self->vmSpace->stdioStream );
	DaoStream_Flush( self->vmSpace->errorStream );
	fflush( stdout );
	fflush( stderr );
}
int DaoProcess_Compile( DaoProcess *self, DaoNamespace *ns, const char *src )
{
	DaoParser *parser = DaoVmSpace_AcquireParser( self->vmSpace );
	int res;

	parser->nameSpace = ns;
	DString_Assign( parser->fileName, ns->name );
	res = DaoParser_LexCode( parser, src, 1 ) && DaoParser_ParseScript( parser );
	DaoVmSpace_ReleaseParser( self->vmSpace, parser );
	DaoProcess_FlushStdStreams( self );
	return res;
}
int DaoProcess_Eval( DaoProcess *self, DaoNamespace *ns, const char *source )
{
	DaoParser *parser = DaoVmSpace_AcquireParser( self->vmSpace );
	DaoRoutine *rout;
	int res;

	parser->autoReturn = 1;
	parser->nameSpace = ns;
	DString_SetMBS( parser->fileName, "code string" );
	res = DaoParser_LexCode( parser, source, 1 ) && DaoParser_ParseScript( parser );
	DaoVmSpace_ReleaseParser( self->vmSpace, parser );
	DaoProcess_FlushStdStreams( self );
	if( res == 0 ) return 0;
	rout = ns->mainRoutines->items.pRoutine[ ns->mainRoutines->size-1 ];
	if( DaoProcess_Call( self, rout, NULL, NULL, 0 ) ) return 0;
	return ns->mainRoutines->size;
}
int DaoProcess_Call( DaoProcess *self, DaoRoutine *M, DaoValue *O, DaoValue *P[], int N )
{
	int ret = DaoProcess_PushCallable( self, M, O, P, N );
	if( ret ) goto Done;
	/* no return value to the previous stack frame */
	DaoProcess_InterceptReturnValue( self );
	ret = DaoProcess_Execute( self ) == 0 ? DAO_ERROR : 0;
Done:
	DaoProcess_FlushStdStreams( self );
	return ret;
}
void DaoProcess_CallFunction( DaoProcess *self, DaoRoutine *func, DaoValue *p[], int n )
{
	daoint m = self->factory->size;
	func->pFunc( self, p, n );
	if( self->factory->size > m ) DArray_Erase( self->factory, m, -1 );
}
void DaoProcess_Stop( DaoProcess *self )
{
	self->stopit = 1;
}
DaoValue* DaoProcess_GetReturned( DaoProcess *self )
{
	return self->stackValues[0];
}
void DaoProcess_AcquireCV( DaoProcess *self )
{
	self->depth += 1;
}
void DaoProcess_ReleaseCV( DaoProcess *self )
{
	self->depth -= 1;
}
static void DaoProcess_PushDefers( DaoProcess *self, DaoValue *result )
{
	DaoVariable *variable = NULL;
	daoint i;
	self->activeCode = NULL;
	for(i=self->topFrame->deferBase; i<self->defers->size; ++i){
		DaoRoutine *closure = self->defers->items.pRoutine[i];
		DaoProcess_PushRoutine( self, closure, NULL );
		self->topFrame->returning = -1;
		self->topFrame->exceptBase = self->exceptions->size;
		if( closure->attribs & DAO_ROUT_PASSRET ){
			DaoVariable *var = closure->body->svariables->items.pVar[0];
			if( variable ){
				GC_ShiftRC( variable, var );
				closure->body->svariables->items.pVar[0] = variable;
			}else{
				variable = var;
				if( result == NULL && var->dtype ) result = var->dtype->value;
				if( result == NULL ) result = dao_none_value;
				DaoValue_Move( result, & var->value, var->dtype );
			}
		}
	}
}

static daoint DaoArray_ComputeIndex( DaoArray *self, DaoValue *ivalues[], int count )
{
	daoint *dims, *dmac, i, j, id = 0;
	if( count > self->ndim ) return -1;
	dims = self->dims;
	dmac = self->dims + self->ndim;
	for(i=0; i<count; i++){
		j = ivalues[i]->xInteger.value;
		if( j <0 ) j += dims[i];
		if( j <0 || j >= dims[i] ) return -1;
		id += j * dmac[i];
	}
	return id;
}


#define IntegerOperand( i ) locVars[i]->xInteger.value
#define FloatOperand( i )   locVars[i]->xFloat.value
#define DoubleOperand( i )  locVars[i]->xDouble.value
#define ComplexOperand( i ) locVars[i]->xComplex.value

#define ArrayArrayValue( array, up, id ) array->items.pArray[ up ]->items.pValue[ id ]

static int DaoProcess_Move( DaoProcess *self, DaoValue *A, DaoValue **C, DaoType *t );
static void DaoProcess_AdjustCodes( DaoProcess *self, int options );

#ifdef DAO_WITH_CONCURRENT
int DaoCallServer_MarkActiveProcess( DaoProcess *process, int active );
#endif


#ifndef WITHOUT_DIRECT_THREADING
#if !defined( __GNUC__ ) || defined( __STRICT_ANSI__ )
#define WITHOUT_DIRECT_THREADING
#endif
#endif

int DaoProcess_Execute( DaoProcess *self )
{
	DaoJitCallData jitCallData = {NULL};
	DaoStackFrame *rollback = NULL;
	DaoUserHandler *handler = self->vmSpace->userHandler;
	DaoVmSpace *vmSpace = self->vmSpace;
	DaoVmCode *vmcBase, *vmc2, *vmc = NULL;
	DaoStackFrame *topFrame;
	DaoRoutine *routine;
	DaoNamespace *here = NULL;
	DaoClass *host = NULL;
	DaoClass *klass = NULL;
	DaoObject *othis = NULL;
	DaoObject *object = NULL;
	DaoArray *array;
	DArray   *typeVO = NULL;
	DaoProcess *dataVH[DAO_MAX_SECTDEPTH+1] = {0};
	DaoVariable *variable = NULL;
	DaoVariable **svariables = NULL;
	DaoValue  **dataVO = NULL;
	DaoValue **dataCL = NULL;
	DaoValue *value, *vA, *vB, *vC = NULL;
	DaoValue **vA2, **vB2, **vC2 = NULL;
	DaoValue **vref;
	DaoValue **locVars;
	DaoType **locTypes;
	DaoType *abtp;
	DaoTuple *tuple;
	DaoList *list;
	DString *str;
	complex16 com = {0,0};
	complex16 czero = {0,0};
	int invokehost = handler && handler->InvokeHost;
	int print, active = self->active;
	daoint exceptCount0 = self->exceptions->size;
	daoint exceptCount = 0;
	daoint gotoCount = 0;
	daoint i, j, id, size;
	daoint inum=0;
	float fnum=0;
	double AA, BB, dnum=0;
	complex16 acom, bcom;
	DaoStackFrame *base;

#ifndef WITHOUT_DIRECT_THREADING
	static void *labels[] = {
		&& LAB_NOP ,
		&& LAB_DATA ,
		&& LAB_GETCL , && LAB_GETCK , && LAB_GETCG ,
		&& LAB_GETVH , && LAB_GETVS , && LAB_GETVO , && LAB_GETVK , && LAB_GETVG ,
		&& LAB_GETI  , && LAB_GETDI , && LAB_GETMI , && LAB_GETF  ,
		&& LAB_SETVH , && LAB_SETVS , && LAB_SETVO , && LAB_SETVK , && LAB_SETVG ,
		&& LAB_SETI  , && LAB_SETDI , && LAB_SETMI , && LAB_SETF  ,
		&& LAB_LOAD  , && LAB_CAST , && LAB_MOVE ,
		&& LAB_NOT , && LAB_MINUS , && LAB_TILDE , && LAB_SIZE ,
		&& LAB_ADD , && LAB_SUB ,
		&& LAB_MUL , && LAB_DIV ,
		&& LAB_MOD , && LAB_POW ,
		&& LAB_AND , && LAB_OR ,
		&& LAB_LT , && LAB_LE ,
		&& LAB_EQ , && LAB_NE , && LAB_IN ,
		&& LAB_BITAND , && LAB_BITOR ,
		&& LAB_BITXOR , && LAB_BITLFT ,
		&& LAB_BITRIT , && LAB_CHECK ,
		&& LAB_NAMEVA , && LAB_PAIR ,
		&& LAB_TUPLE  , && LAB_LIST ,
		&& LAB_MAP    , && LAB_HASH ,
		&& LAB_VECTOR , && LAB_MATRIX ,
		&& LAB_APLIST , && LAB_APVECTOR ,
		&& LAB_PACK  , && LAB_MPACK ,
		&& LAB_ROUTINE ,
		&& LAB_GOTO ,
		&& LAB_SWITCH , && LAB_CASE ,
		&& LAB_ITER , && LAB_TEST ,
		&& LAB_MATH ,
		&& LAB_CALL , && LAB_MCALL ,
		&& LAB_RETURN , && LAB_YIELD ,
		&& LAB_EVAL , && LAB_SECT ,
		&& LAB_JITC ,
		&& LAB_DEBUG ,

		&& LAB_DATA_I , && LAB_DATA_F , && LAB_DATA_D , && LAB_DATA_C ,
		&& LAB_GETCL_I , && LAB_GETCL_F , && LAB_GETCL_D , && LAB_GETCL_C ,
		&& LAB_GETCK_I , && LAB_GETCK_F , && LAB_GETCK_D , && LAB_GETCK_C ,
		&& LAB_GETCG_I , && LAB_GETCG_F , && LAB_GETCG_D , && LAB_GETCG_C ,
		&& LAB_GETVH_I , && LAB_GETVH_F , && LAB_GETVH_D , && LAB_GETVH_C ,
		&& LAB_GETVS_I , && LAB_GETVS_F , && LAB_GETVS_D , && LAB_GETVS_C ,
		&& LAB_GETVO_I , && LAB_GETVO_F , && LAB_GETVO_D , && LAB_GETVO_C ,
		&& LAB_GETVK_I , && LAB_GETVK_F , && LAB_GETVK_D , && LAB_GETVK_C ,
		&& LAB_GETVG_I , && LAB_GETVG_F , && LAB_GETVG_D , && LAB_GETVG_C ,
		&& LAB_SETVH_II , && LAB_SETVH_FF , && LAB_SETVH_DD , && LAB_SETVH_CC ,
		&& LAB_SETVS_II , && LAB_SETVS_FF , && LAB_SETVS_DD , && LAB_SETVS_CC ,
		&& LAB_SETVO_II , && LAB_SETVO_FF , && LAB_SETVO_DD , && LAB_SETVO_CC ,
		&& LAB_SETVK_II , && LAB_SETVK_FF , && LAB_SETVK_DD , && LAB_SETVK_CC ,
		&& LAB_SETVG_II , && LAB_SETVG_FF , && LAB_SETVG_DD , && LAB_SETVG_CC ,

		&& LAB_MOVE_II , && LAB_MOVE_IF , && LAB_MOVE_ID ,
		&& LAB_MOVE_FI , && LAB_MOVE_FF , && LAB_MOVE_FD ,
		&& LAB_MOVE_DI , && LAB_MOVE_DF , && LAB_MOVE_DD ,
		&& LAB_MOVE_CI , && LAB_MOVE_CF , && LAB_MOVE_CD ,
		&& LAB_MOVE_CC , && LAB_MOVE_SS , && LAB_MOVE_PP , && LAB_MOVE_XX ,
		&& LAB_NOT_I , && LAB_NOT_F , && LAB_NOT_D ,
		&& LAB_MINUS_I , && LAB_MINUS_F , && LAB_MINUS_D , && LAB_MINUS_C ,
		&& LAB_TILDE_I , && LAB_TILDE_C ,

		&& LAB_ADD_III , && LAB_SUB_III , && LAB_MUL_III , && LAB_DIV_III ,
		&& LAB_MOD_III , && LAB_POW_III , && LAB_AND_III , && LAB_OR_III ,
		&& LAB_LT_III , && LAB_LE_III , && LAB_EQ_III , && LAB_NE_III ,
		&& LAB_BITAND_III , && LAB_BITOR_III , && LAB_BITXOR_III ,
		&& LAB_BITLFT_III , && LAB_BITRIT_III ,

		&& LAB_ADD_FFF , && LAB_SUB_FFF , && LAB_MUL_FFF , && LAB_DIV_FFF ,
		&& LAB_MOD_FFF , && LAB_POW_FFF , && LAB_AND_FFF , && LAB_OR_FFF ,
		&& LAB_LT_IFF , && LAB_LE_IFF , && LAB_EQ_IFF , && LAB_NE_IFF ,

		&& LAB_ADD_DDD , && LAB_SUB_DDD , && LAB_MUL_DDD , && LAB_DIV_DDD ,
		&& LAB_MOD_DDD , && LAB_POW_DDD , && LAB_AND_DDD , && LAB_OR_DDD ,
		&& LAB_LT_IDD , && LAB_LE_IDD , && LAB_EQ_IDD , && LAB_NE_IDD ,

		&& LAB_ADD_CCC , && LAB_SUB_CCC ,
		&& LAB_MUL_CCC , && LAB_DIV_CCC ,
		&& LAB_EQ_ICC , && LAB_NE_ICC ,

		&& LAB_ADD_SSS ,
		&& LAB_LT_ISS , && LAB_LE_ISS ,
		&& LAB_EQ_ISS , && LAB_NE_ISS ,

		&& LAB_GETI_LI , && LAB_SETI_LI , && LAB_GETI_SI , && LAB_SETI_SII ,
		&& LAB_GETI_LII , && LAB_GETI_LFI , && LAB_GETI_LDI ,
		&& LAB_GETI_LCI , && LAB_GETI_LSI ,
		&& LAB_SETI_LIII , && LAB_SETI_LFIF , && LAB_SETI_LDID ,
		&& LAB_SETI_LCIC , && LAB_SETI_LSIS ,
		&& LAB_GETI_AII , && LAB_GETI_AFI , && LAB_GETI_ADI , && LAB_GETI_ACI ,
		&& LAB_SETI_AIII , && LAB_SETI_AFIF , && LAB_SETI_ADID , && LAB_SETI_ACIC ,

		&& LAB_GETI_TI , && LAB_SETI_TI ,

		&& LAB_GETF_TI , && LAB_GETF_TF ,
		&& LAB_GETF_TD , && LAB_GETF_TC , && LAB_GETF_TX ,
		&& LAB_SETF_TII , && LAB_SETF_TFF , && LAB_SETF_TDD , && LAB_SETF_TCC ,
		&& LAB_SETF_TSS , && LAB_SETF_TPP , && LAB_SETF_TXX ,

		&& LAB_GETMI_AII , && LAB_GETMI_AFI ,
		&& LAB_GETMI_ADI , && LAB_GETMI_ACI ,
		&& LAB_SETMI_AIII , && LAB_SETMI_AFIF ,
		&& LAB_SETMI_ADID , && LAB_SETMI_ACIC ,

		&& LAB_GETF_CX , && LAB_SETF_CX ,

		&& LAB_GETF_KC , && LAB_GETF_KG ,
		&& LAB_GETF_OC , && LAB_GETF_OG , && LAB_GETF_OV ,
		&& LAB_SETF_KG , && LAB_SETF_OG , && LAB_SETF_OV ,

		&& LAB_GETF_KCI , && LAB_GETF_KCF , && LAB_GETF_KCD , && LAB_GETF_KCC ,
		&& LAB_GETF_KGI , && LAB_GETF_KGF , && LAB_GETF_KGD , && LAB_GETF_KGC ,
		&& LAB_GETF_OCI , && LAB_GETF_OCF , && LAB_GETF_OCD , && LAB_GETF_OCC ,
		&& LAB_GETF_OGI , && LAB_GETF_OGF , && LAB_GETF_OGD , && LAB_GETF_OGC ,
		&& LAB_GETF_OVI , && LAB_GETF_OVF , && LAB_GETF_OVD , && LAB_GETF_OVC ,

		&& LAB_SETF_KGII , && LAB_SETF_KGFF , && LAB_SETF_KGDD , && LAB_SETF_KGCC ,
		&& LAB_SETF_OGII , && LAB_SETF_OGFF , && LAB_SETF_OGDD , && LAB_SETF_OGCC ,
		&& LAB_SETF_OVII , && LAB_SETF_OVFF , && LAB_SETF_OVDD , && LAB_SETF_OVCC ,

		&& LAB_TEST_I , && LAB_TEST_F , && LAB_TEST_D ,
		&& LAB_MATH_I , && LAB_MATH_F , && LAB_MATH_D ,
		&& LAB_CHECK_ST ,

		&& LAB_SAFE_GOTO
	};
#endif

#ifndef WITHOUT_DIRECT_THREADING

#define OPBEGIN() goto *labels[ vmc->code ];
#define OPCASE( name ) LAB_##name :
#define OPNEXT() goto *labels[ (++vmc)->code ];
#define OPJUMP() goto *labels[ vmc->code ];
#define OPDEFAULT()
#define OPEND()

#else

#if defined( __GNUC__ ) && !defined( __STRICT_ANSI__ )
#warning "=========================================="
#warning "=========================================="
#warning "  NOT USING DIRECT THREADING"
#warning "=========================================="
#warning "=========================================="
#endif

#define OPBEGIN() for(;;){ switch( vmc->code )
#define OPCASE( name ) case DVM_##name :
#define OPNEXT() break;
#define OPJUMP() continue;
#define OPDEFAULT() default:
#define OPEND() vmc++; }

#if 0
#define OPBEGIN() for(;;){ printf("%3i:", (i=vmc-vmcBase) ); DaoVmCodeX_Print( *topFrame->routine->body->annotCodes->items.pVmc[i], NULL ); switch( vmc->code )
#endif

#endif


	if( self->topFrame == self->firstFrame ) goto ReturnFalse;
	rollback = self->topFrame->prev;
	base = self->topFrame;
	if( self->status == DAO_PROCESS_SUSPENDED ) base = self->firstFrame->next;

CallEntry:

	topFrame = self->topFrame;
	routine = topFrame->routine;

	if( topFrame == base->prev ){
		self->status = DAO_PROCESS_FINISHED;
		if( self->exceptions->size > 0 ) goto FinishProcess;
		/*if( eventHandler ) eventHandler->mainRoutineExit(); */
		goto ReturnTrue;
	}
	if( self->topFrame->state & DVM_FRAME_FINISHED ) goto FinishCall;

	if( self->cache == NULL || self->cache->fails > 10 ){
		//printf( "%12p %9i %9i; ", self->cache, self->cache->fails, self->cache->count );
		self->cache = DaoDataCache_Acquire( self->cache, 0 );
		//printf( "%12p %9i\n", self->cache, self->cache->count );
	}

	if( routine->pFunc ){
		DaoValue **p = self->stackValues + topFrame->stackBase;
		if( self->status == DAO_PROCESS_STACKED ){
			DaoProcess_CallFunction( self, topFrame->routine, p, topFrame->parCount );
		}
		DaoProcess_PopFrame( self );
		goto CallEntry;
	}
#if 0
	if( (vmSpace->options & DAO_OPTION_SAFE) && self->topFrame->index >= 100 ){
		DaoProcess_RaiseException( self, DAO_ERROR,
				"too deep recursion for safe running mode." );
		goto FinishProcess;
	}
#endif


#if 0
	if( ROUT_HOST_TID( routine ) == DAO_OBJECT )
		printf("class name = %s\n", routine->routHost->aux->xClass.className->mbs);
	printf("routine name = %s\n", routine->routName->mbs);
	printf("number of instruction: %i\n", routine->body->vmCodes->size );
	printf("entry instruction: %i\n", self->topFrame->entry );
	if( routine->routType ) printf("routine type = %s\n", routine->routType->name->mbs);
#endif

	if( self->stopit | vmSpace->stopit ) goto FinishProcess;
	//XXX if( invokehost ) handler->InvokeHost( handler, topCtx );

	if( (vmSpace->options & DAO_OPTION_DEBUG) | (routine->body->mode & DAO_OPTION_DEBUG) )
		DaoProcess_AdjustCodes( self, vmSpace->options );

	vmcBase = topFrame->codes;
	id = self->topFrame->entry;
	vmc = vmcBase + id;
	self->stopit = 0;
	self->activeCode = vmc;
	self->activeRoutine = routine;
	self->activeObject = topFrame->object;
	self->activeValues = self->stackValues + topFrame->stackBase;
	self->activeTypes = routine->body->regType->items.pType;
	self->activeNamespace = routine->nameSpace;

	if( id >= routine->body->vmCodes->size ){
		if( id == 0 ){
			DString_SetMBS( self->mbstring, "Not implemented function, " );
			DString_Append( self->mbstring, routine->routName );
			DString_AppendMBS( self->mbstring, "()" );
			DaoProcess_RaiseException( self, DAO_ERROR, self->mbstring->mbs );
			goto FinishProcess;
		}
		goto FinishCall;
	}

	if( !(topFrame->state & DVM_FRAME_RUNNING) ){
		topFrame->deferBase = self->defers->size;
		topFrame->exceptBase = self->exceptions->size;
		if( (routine->attribs & (DAO_ROUT_PRIVATE|DAO_ROUT_PROTECTED)) && topFrame->prev ){
			uchar_t priv = routine->attribs & DAO_ROUT_PRIVATE;
			if( routine->routHost ){
				DaoObject *obj = topFrame->prev->object;
				//TODO: permission check before tail call optimization!
				//XXX fltk/demo/table.dao:
				//if( priv == 0 && obj == NULL ) goto CallNotPermitted;
				if( priv && obj && obj->defClass->objType != routine->routHost ) goto CallNotPermitted;
			}else if( priv && routine->nameSpace != topFrame->prev->routine->nameSpace ){
				goto CallNotPermitted;
			}
		}
	}
	if( vmc->code == DVM_EVAL && (topFrame->state & DVM_FRAME_RUNNING) ){
		if( self->exceptions->size > topFrame->exceptBase ){
			if( vmc->b == 0 ) goto FinishCall;
			DArray_Erase( self->exceptions, topFrame->exceptBase, -1 );
			if( vmc->b == 1 && DaoProcess_SetValue( self, vmc->c, locVars[vmc->a] ) == 0 ){
				DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "invalid default value" );
			}
			vmc += vmc->b == 2;
		}
		vmc += 1;
	}

	exceptCount = self->exceptions->size;
	if( self->exceptions->size > topFrame->exceptBase ) goto FinishCall;

	if( self->status == DAO_PROCESS_SUSPENDED &&
			(vmc->code == DVM_CALL || vmc->code == DVM_MCALL || vmc->code == DVM_YIELD) ){
		DaoFuture *future = self->future;
		DaoTuple *tuple;
		int finished;
		switch( self->pauseType ){
		case DAO_PAUSE_NONE :
			break;
		case DAO_PAUSE_FUTURE_VALUE :
			finished = future->precond->state == DAO_CALL_FINISHED;
			DaoProcess_PutValue( self, finished ? future->precond->value : dao_none_value );
			break;
		case DAO_PAUSE_FUTURE_WAIT :
			DaoProcess_PutInteger( self, future->precond->state == DAO_CALL_FINISHED );
			break;
		case DAO_PAUSE_CHANNEL_SEND :
			DaoProcess_PutInteger( self, future->timeout );
			break;
		case DAO_PAUSE_CHANNEL_RECEIVE :
			tuple = DaoProcess_PutTuple( self, 0 );
			DaoTuple_SetItem( tuple, future->message ? future->message : dao_none_value, 0 );
			tuple->items[1]->xEnum.value = future->aux1 ? 2 : future->timeout != 0;
			break;
		case DAO_PAUSE_CHANFUT_SELECT :
			tuple = DaoProcess_PutTuple( self, 0 );
			DaoTuple_SetItem( tuple, future->selected ? future->selected : dao_none_value, 0 );
			DaoTuple_SetItem( tuple, future->message ? future->message : dao_none_value, 1 );
			tuple->items[2]->xEnum.value = future->aux1 ? 2 : future->timeout != 0;
			break;
		default: break;
		}
		vmc ++;
	}
	topFrame->state |= DVM_FRAME_RUNNING;
	self->status = DAO_PROCESS_RUNNING;
	self->pauseType = DAO_PAUSE_NONE;
	host = NULL;
	here = routine->nameSpace;
	othis = topFrame->object;
	locVars = self->activeValues;
	locTypes = self->activeTypes;
	dataCL = routine->routConsts->items.items.pValue;
	svariables = routine->body->svariables->items.pVar;
	if( routine->body->jitData ){
		jitCallData.localValues = locVars;
		jitCallData.localConsts = routine->routConsts->items.items.pValue;
		jitCallData.globalValues = here->variables->items.pVar;
		jitCallData.globalConsts = here->constants->items.pConst;
		jitCallData.processes = dataVH;
	}
	if( ROUT_HOST_TID( routine ) == DAO_OBJECT ){
		host = & routine->routHost->aux->xClass;
		jitCallData.classValues = host->variables->items.pVar;
		jitCallData.classConsts = host->constants->items.pConst;
		if( !(routine->attribs & DAO_ROUT_STATIC) ){
			dataVO = othis->objValues;
			typeVO = host->instvars;
			jitCallData.objectValues = dataVO;
		}
	}
	if( topFrame->outer ){
		DaoStackFrame *frame = topFrame;
		for(i=1; (i<=DAO_MAX_SECTDEPTH) && frame->outer; i++){
			dataVH[i] = frame->outer;
			frame = frame->sect;
		}
	}

	OPBEGIN(){
		OPCASE( NOP ){
			if( self->stopit | vmSpace->stopit ) goto FinishProcess;
		}OPNEXT() OPCASE( DATA ){
			if( vmc->a == DAO_NONE ){
				GC_ShiftRC( dao_none_value, locVars[ vmc->c ] );
				locVars[ vmc->c ] = dao_none_value;
			}else{
				value = locVars[vmc->c];
				if( value == NULL || value->type != vmc->a ){
					DaoValue *tmp = (DaoValue*) DaoComplex_New(czero);
					tmp->type = vmc->a;
					GC_ShiftRC( tmp, value );
					locVars[ vmc->c ] = value = tmp;
				}
				switch( vmc->a ){
				case DAO_COMPLEX :
					value->xComplex.value.real = 0;
					value->xComplex.value.imag = vmc->b;
					break;
				case DAO_INTEGER : value->xInteger.value = vmc->b; break;
				case DAO_FLOAT  : value->xFloat.value = vmc->b; break;
				case DAO_DOUBLE : value->xDouble.value = vmc->b; break;
				default : break;
				}
			}
		}OPNEXT() OPCASE( GETCL ){
			/* All GETX instructions assume the C regisgter is an intermediate register! */
			value = dataCL[ vmc->b ];
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETCK ){
			value = host->constants->items.pConst[ vmc->b ]->value;
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETCG ){
			value = here->constants->items.pConst[ vmc->b ]->value;
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETVH ){
			GC_ShiftRC( dataVH[ vmc->a ]->activeValues[ vmc->b ], locVars[ vmc->c ] );
			locVars[ vmc->c ] = dataVH[ vmc->a ]->activeValues[ vmc->b ];
		}OPNEXT() OPCASE( GETVS ){
			value = svariables[ vmc->b ]->value;
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETVO ){
			GC_ShiftRC( dataVO[ vmc->b ], locVars[ vmc->c ] );
			locVars[ vmc->c ] = dataVO[ vmc->b ];
		}OPNEXT() OPCASE( GETVK ){
			value = host->variables->items.pVar[ vmc->b ]->value;
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETVG ){
			value = here->variables->items.pVar[ vmc->b ]->value;
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETI ) OPCASE( GETDI ) OPCASE( GETMI ){
			DaoProcess_DoGetItem( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( GETF ){
			DaoProcess_DoGetField( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( SETVH ){
			abtp = dataVH[ vmc->c ]->activeTypes[ vmc->b ];
			if( DaoProcess_Move( self, locVars[vmc->a], dataVH[ vmc->c ]->activeValues + vmc->b, abtp ) ==0 )
				goto CheckException;
		}OPNEXT() OPCASE( SETVS ){
			variable = svariables[ vmc->b ];
			if( DaoProcess_Move( self, locVars[vmc->a], & variable->value, variable->dtype ) ==0 )
				goto CheckException;
		}OPNEXT() OPCASE( SETVO ){
			abtp = typeVO->items.pVar[ vmc->b ]->dtype;
			if( DaoProcess_Move( self, locVars[vmc->a], dataVO + vmc->b, abtp ) ==0 )
				goto CheckException;
		}OPNEXT() OPCASE( SETVK ){
			variable = host->variables->items.pVar[ vmc->b ];
			if( DaoProcess_Move( self, locVars[vmc->a], & variable->value, variable->dtype ) ==0 ) goto CheckException;
		}OPNEXT() OPCASE( SETVG ){
			variable = here->variables->items.pVar[ vmc->b ];
			if( DaoProcess_Move( self, locVars[vmc->a], & variable->value, variable->dtype ) ==0 )
				goto CheckException;
		}OPNEXT() OPCASE( SETI ) OPCASE( SETDI ) OPCASE( SETMI ){
			DaoProcess_DoSetItem( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( SETF ){
			DaoProcess_DoSetField( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( LOAD ){
			if( (vA = locVars[ vmc->a ]) ){
				/* mt.run(3)::{ mt.critical::{} }: the inner functional will be compiled
				 * as a LOAD and RETURN, but the inner functional will not return anything,
				 * so the first operand of LOAD will be NULL! */
				if( (vA->xBase.trait & DAO_VALUE_CONST) == 0 ){
					GC_ShiftRC( vA, locVars[ vmc->c ] );
					locVars[ vmc->c ] = vA;
				}else{
					DaoValue_Copy( vA, & locVars[ vmc->c ] );
				}
			}
		}OPNEXT() OPCASE( CAST ){
			self->activeCode = vmc;
			DaoProcess_DoCast( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( MOVE ){
			DaoProcess_Move( self, locVars[ vmc->a ], & locVars[ vmc->c ], locTypes[vmc->c] );
			goto CheckException;
		}OPNEXT()
		OPCASE( ADD )
			OPCASE( SUB )
			OPCASE( MUL )
			OPCASE( DIV )
			OPCASE( MOD )
			OPCASE( POW ){
				self->activeCode = vmc;
				DaoProcess_DoBinArith( self, vmc );
				goto CheckException;
			}OPNEXT()
		OPCASE( AND )
			OPCASE( OR )
			OPCASE( LT )
			OPCASE( LE )
			OPCASE( EQ )
			OPCASE( NE ){
				self->activeCode = vmc;
				DaoProcess_DoBinBool( self, vmc );
				goto CheckException;
			}OPNEXT()
		OPCASE( IN ){
			self->activeCode = vmc;
			DaoProcess_DoInTest( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( NOT ) OPCASE( MINUS ){
			self->activeCode = vmc;
			DaoProcess_DoUnaArith( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( BITAND ) OPCASE( BITOR ) OPCASE( BITXOR ){
			self->activeCode = vmc;
			DaoProcess_DoBitLogic( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( BITLFT ) OPCASE( BITRIT ){
			self->activeCode = vmc;
			DaoProcess_DoBitShift( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( TILDE ){
			self->activeCode = vmc;
			DaoProcess_DoBitFlip( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( SIZE ){
			vA = locVars[ vmc->a ];
			vC = locVars[ vmc->c ];
			switch( vA->type ){
			case DAO_NONE    : vC->xInteger.value = 0; break;
			case DAO_INTEGER : vC->xInteger.value = sizeof(daoint); break;
			case DAO_FLOAT   : vC->xInteger.value = sizeof(float); break;
			case DAO_DOUBLE  : vC->xInteger.value = sizeof(double); break;
			case DAO_COMPLEX : vC->xInteger.value = sizeof(complex16); break;
			case DAO_LONG    : vC->xInteger.value = vA->xLong.value->size; break;
			case DAO_ENUM    : vC->xInteger.value = sizeof(int); break; break;
			case DAO_STRING  : vC->xInteger.value = vA->xString.data->size; break;
			case DAO_LIST    : vC->xInteger.value = vA->xList.items.size; break;
			case DAO_MAP     : vC->xInteger.value = vA->xMap.items->size; break;
			case DAO_TUPLE   : vC->xInteger.value = vA->xTuple.size; break;
#ifdef DAO_WITH_NUMARRAY
			case DAO_ARRAY   : vC->xInteger.value = vA->xArray.size; break;
#endif
			default : goto RaiseErrorInvalidOperation;
			}
		}OPNEXT() OPCASE( CHECK ){
			DaoProcess_DoCheck( self, vmc );
		}OPNEXT() OPCASE( NAMEVA ){
			DaoProcess_BindNameValue( self, vmc );
		}OPNEXT() OPCASE( PAIR ){
			self->activeCode = vmc;
			DaoProcess_DoPair( self, vmc );
		}OPNEXT() OPCASE( TUPLE ){
			self->activeCode = vmc;
			DaoProcess_DoTuple( self, vmc );
		}OPNEXT() OPCASE( LIST ){
			self->activeCode = vmc;
			DaoProcess_DoList( self, vmc );
		}OPNEXT() OPCASE( MAP ) OPCASE( HASH ){
			self->activeCode = vmc;
			DaoProcess_DoMap( self, vmc );
		}OPNEXT() OPCASE( VECTOR ){
			DaoProcess_DoVector( self, vmc );
		}OPNEXT() OPCASE( MATRIX ){
			DaoProcess_DoMatrix( self, vmc );
		}OPNEXT() OPCASE( APLIST ){
			DaoProcess_DoAPList( self, vmc );
		}OPNEXT() OPCASE( APVECTOR ){
			DaoProcess_DoAPVector( self, vmc );
		}OPNEXT() OPCASE( PACK ) OPCASE( MPACK ){
			DaoProcess_DoPacking( self, vmc );
		}OPNEXT() OPCASE( CASE ) OPCASE( GOTO ){
			vmc = vmcBase + vmc->b;
		}OPJUMP() OPCASE( SWITCH ){
			vmc = DaoProcess_DoSwitch( self, vmc );
		}OPJUMP() OPCASE( ITER ){
			self->activeCode = vmc;
			DaoProcess_DoIter( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( TEST ){
			vA = locVars[ vmc->a ];
			switch( vA->type ){
			case DAO_NONE :
				vmc = vmcBase + vmc->b; break;
			case DAO_INTEGER :
				vmc = vA->xInteger.value ? vmc+1 : vmcBase + vmc->b; break;
			case DAO_FLOAT   :
				vmc = vA->xFloat.value ? vmc+1 : vmcBase + vmc->b; break;
			case DAO_DOUBLE  :
				vmc = vA->xDouble.value ? vmc+1 : vmcBase + vmc->b; break;
			case DAO_COMPLEX :
				vmc = (vA->xComplex.value.real || vA->xComplex.value.imag) ? vmc+1 : vmcBase + vmc->b;
				break;
			case DAO_LONG :
				j = vA->xLong.value->size >1 || (vA->xLong.value->size ==1 && vA->xLong.value->data[0]);
				vmc = j ? vmc+1 : vmcBase + vmc->b;
				break;
			case DAO_ENUM  :
				vmc = vA->xEnum.value ? vmc+1 : vmcBase + vmc->b;
				break;
			case DAO_CTYPE :
			case DAO_CSTRUCT :
				vmc += 1;
				break;
			case DAO_CDATA :
				vmc = vA->xCdata.data ? vmc+1 : vmcBase + vmc->b;
				break;
			default :
				goto RaiseErrorInvalidOperation;
			}
		}OPJUMP() OPCASE( MATH ){
			if( DaoVM_DoMath( self, vmc, locVars[ vmc->c ], locVars[vmc->b] ) )
				goto RaiseErrorInvalidOperation;
		}OPNEXT() OPCASE( CALL ) OPCASE( MCALL ){
			if( self->stopit | vmSpace->stopit ) goto FinishProcess;
			DaoProcess_DoCall( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( ROUTINE ){
			self->activeCode = vmc;
			DaoProcess_MakeRoutine( self, vmc );
			goto CheckException;
		}OPNEXT() OPCASE( JITC ){
			jitCallData.localValues = locVars;
			jitCallData.globalValues = here->variables->items.pVar;
			dao_jit.Execute( self, & jitCallData, vmc->a );
			if( self->exceptions->size > exceptCount ) goto CheckException;
			vmc += vmc->b;
			OPJUMP()
				/*
				   dbase = (DaoValue*)inum;
				   printf( "jitc: %#x, %i\n", inum, dbase->type );
				 */
		}OPNEXT() OPCASE( RETURN ){
			self->activeCode = vmc;
			value = DaoProcess_DoReturn( self, vmc );
			if( self->defers->size > self->topFrame->deferBase ){
				self->topFrame->state |= DVM_FRAME_FINISHED;
				DaoProcess_PushDefers( self, value );
				goto CallEntry;
			}
			if( self->stopit | vmSpace->stopit ) goto FinishProcess;
			goto FinishCall;
		}OPNEXT() OPCASE( YIELD ){
			self->activeCode = vmc;
			if( routine->routType->cbtype == NULL ){
				DaoProcess_RaiseException( self, DAO_ERROR, "Not in code section methods." );
				goto CheckException;
			}
			if( DaoProcess_PushSectionFrame( self ) == NULL ){
				printf( "No code section is found\n" ); //XXX
				goto FinishProcess;
			}
			self->topFrame->state = DVM_FRAME_SECT;
			vmc2 = self->topFrame->codes + self->topFrame->entry - 1;
			locVars = self->stackValues + topFrame->stackBase;
			for(i=0; i<vmc2->b; i++){
				if( i >= vmc->b ) break;
				if( DaoProcess_SetValue( self, vmc2->a + i, locVars[vmc->a + i] ) == 0 ){
					DaoProcess_RaiseException( self, DAO_ERROR_PARAM, "invalid yield" );
				}
			}
			self->status = DAO_PROCESS_STACKED;
			goto CheckException;
		}OPCASE( DEBUG ){
			if( self->stopit | vmSpace->stopit ) goto FinishProcess;
			if( (vmSpace->options & DAO_OPTION_DEBUG ) ){
				self->activeCode = vmc;
				if( handler && handler->StdlibDebug ) handler->StdlibDebug( handler, self );
				goto CheckException;
			}
		}OPNEXT() OPCASE( EVAL ){
			self->activeCode = vmc;
			if( DaoProcess_PushSectionFrame( self ) == NULL ){
				printf( "No code section is found\n" ); //XXX
				goto FinishProcess;
			}
			topFrame->entry = vmc - topFrame->codes;
			self->topFrame->state = DVM_FRAME_SECT;
			goto CallEntry;
		}OPNEXT() OPCASE( SECT ){
			goto ReturnFalse;
		}OPNEXT() OPCASE( DATA_I ){
			locVars[ vmc->c ]->xInteger.value = vmc->b;
		}OPNEXT() OPCASE( DATA_F ){
			locVars[ vmc->c ]->xFloat.value = vmc->b;
		}OPNEXT() OPCASE( DATA_D ){
			locVars[ vmc->c ]->xDouble.value = vmc->b;
		}OPNEXT() OPCASE( DATA_C ){
			complex16 *com = & locVars[ vmc->c ]->xComplex.value;
			com->real = 0; com->imag = vmc->b;
		}OPNEXT() OPCASE( GETCL_I ){
			locVars[ vmc->c ]->xInteger.value = dataCL[ vmc->b ]->xInteger.value;
		}OPNEXT() OPCASE( GETCL_F ){
			locVars[ vmc->c ]->xFloat.value = dataCL[ vmc->b ]->xFloat.value;
		}OPNEXT() OPCASE( GETCL_D ){
			locVars[ vmc->c ]->xDouble.value = dataCL[ vmc->b ]->xDouble.value;
		}OPNEXT() OPCASE( GETCL_C ){
			locVars[ vmc->c ]->xComplex.value = dataCL[ vmc->b ]->xComplex.value;
		}OPNEXT() OPCASE( GETCK_I ){
			value = host->constants->items.pConst[vmc->b]->value;;
			locVars[ vmc->c ]->xInteger.value = value->xInteger.value;
		}OPNEXT() OPCASE( GETCK_F ){
			value = host->constants->items.pConst[vmc->b]->value;;
			locVars[ vmc->c ]->xFloat.value = value->xFloat.value;
		}OPNEXT() OPCASE( GETCK_D ){
			value = host->constants->items.pConst[vmc->b]->value;;
			locVars[ vmc->c ]->xDouble.value = value->xDouble.value;
		}OPNEXT() OPCASE( GETCK_C ){
			value = host->constants->items.pConst[vmc->b]->value;;
			locVars[ vmc->c ]->xComplex.value = value->xComplex.value;
		}OPNEXT() OPCASE( GETCG_I ){
			value = here->constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xInteger.value = value->xInteger.value;
		}OPNEXT() OPCASE( GETCG_F ){
			value = here->constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xFloat.value = value->xFloat.value;
		}OPNEXT() OPCASE( GETCG_D ){
			value = here->constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xDouble.value = value->xDouble.value;
		}OPNEXT() OPCASE( GETCG_C ){
			value = here->constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xComplex.value = value->xComplex.value;
		}OPNEXT() OPCASE( GETVH_I ){
			locVars[ vmc->c ]->xInteger.value = dataVH[vmc->a]->activeValues[vmc->b]->xInteger.value;
		}OPNEXT() OPCASE( GETVH_F ){
			locVars[ vmc->c ]->xFloat.value = dataVH[vmc->a]->activeValues[vmc->b]->xFloat.value;
		}OPNEXT() OPCASE( GETVH_D ){
			locVars[ vmc->c ]->xDouble.value = dataVH[vmc->a]->activeValues[vmc->b]->xDouble.value;
		}OPNEXT() OPCASE( GETVH_C ){
			locVars[ vmc->c ]->xComplex.value = dataVH[vmc->a]->activeValues[vmc->b]->xComplex.value;
		}OPNEXT() OPCASE( GETVS_I ){
			locVars[ vmc->c ]->xInteger.value = svariables[ vmc->b ]->value->xInteger.value;
		}OPNEXT() OPCASE( GETVS_F ){
			locVars[ vmc->c ]->xFloat.value = svariables[ vmc->b ]->value->xFloat.value;
		}OPNEXT() OPCASE( GETVS_D ){
			locVars[ vmc->c ]->xDouble.value = svariables[ vmc->b ]->value->xDouble.value;
		}OPNEXT() OPCASE( GETVS_C ){
			locVars[ vmc->c ]->xComplex.value = svariables[ vmc->b ]->value->xComplex.value;
		}OPNEXT() OPCASE( GETVO_I ){
			locVars[ vmc->c ]->xInteger.value = dataVO[ vmc->b ]->xInteger.value;
		}OPNEXT() OPCASE( GETVO_F ){
			locVars[ vmc->c ]->xFloat.value = dataVO[ vmc->b ]->xFloat.value;
		}OPNEXT() OPCASE( GETVO_D ){
			locVars[ vmc->c ]->xDouble.value = dataVO[ vmc->b ]->xDouble.value;
		}OPNEXT() OPCASE( GETVO_C ){
			locVars[ vmc->c ]->xComplex.value = dataVO[ vmc->b ]->xComplex.value;
		}OPNEXT() OPCASE( GETVK_I ){
			IntegerOperand( vmc->c ) = host->variables->items.pVar[vmc->b]->value->xInteger.value;
		}OPNEXT() OPCASE( GETVK_F ){
			FloatOperand( vmc->c ) = host->variables->items.pVar[vmc->b]->value->xFloat.value;
		}OPNEXT() OPCASE( GETVK_D ){
			DoubleOperand( vmc->c ) = host->variables->items.pVar[vmc->b]->value->xDouble.value;
		}OPNEXT() OPCASE( GETVK_C ){
			ComplexOperand( vmc->c ) = host->variables->items.pVar[vmc->b]->value->xComplex.value;
		}OPNEXT() OPCASE( GETVG_I ){
			IntegerOperand( vmc->c ) = here->variables->items.pVar[vmc->b]->value->xInteger.value;
		}OPNEXT() OPCASE( GETVG_F ){
			FloatOperand( vmc->c ) = here->variables->items.pVar[vmc->b]->value->xFloat.value;
		}OPNEXT() OPCASE( GETVG_D ){
			DoubleOperand( vmc->c ) = here->variables->items.pVar[vmc->b]->value->xDouble.value;
		}OPNEXT() OPCASE( GETVG_C ){
			ComplexOperand( vmc->c ) = here->variables->items.pVar[vmc->b]->value->xComplex.value;
		}OPNEXT() OPCASE( SETVH_II ){
			dataVH[ vmc->c ]->activeValues[ vmc->b ]->xInteger.value = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( SETVH_FF ){
			dataVH[ vmc->c ]->activeValues[ vmc->b ]->xFloat.value = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( SETVH_DD ){
			dataVH[ vmc->c ]->activeValues[ vmc->b ]->xDouble.value = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( SETVH_CC ){
			dataVH[ vmc->c ]->activeValues[ vmc->b ]->xComplex.value = ComplexOperand( vmc->a );
		}OPNEXT() OPCASE( SETVS_II ){
			svariables[ vmc->b ]->value->xInteger.value = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( SETVS_FF ){
			svariables[ vmc->b ]->value->xFloat.value = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( SETVS_DD ){
			svariables[ vmc->b ]->value->xDouble.value = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( SETVS_CC ){
			svariables[ vmc->b ]->value->xComplex.value = ComplexOperand( vmc->a );
		}OPNEXT() OPCASE( SETVO_II ){
			dataVO[ vmc->b ]->xInteger.value = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( SETVO_FF ){
			dataVO[ vmc->b ]->xFloat.value = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( SETVO_DD ){
			dataVO[ vmc->b ]->xDouble.value = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( SETVO_CC ){
			dataVO[ vmc->b ]->xComplex.value = ComplexOperand( vmc->a );
		}OPNEXT() OPCASE( SETVK_II ){
			host->variables->items.pVar[vmc->b]->value->xInteger.value = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( SETVK_FF ){
			host->variables->items.pVar[vmc->b]->value->xFloat.value = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( SETVK_DD ){
			host->variables->items.pVar[vmc->b]->value->xDouble.value = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( SETVK_CC ){
			host->variables->items.pVar[vmc->b]->value->xComplex.value = ComplexOperand( vmc->a );
		}OPNEXT() OPCASE( SETVG_II ){
			here->variables->items.pVar[vmc->b]->value->xInteger.value = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( SETVG_FF ){
			here->variables->items.pVar[vmc->b]->value->xFloat.value = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( SETVG_DD ){
			here->variables->items.pVar[vmc->b]->value->xDouble.value = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( SETVG_CC ){
			here->variables->items.pVar[vmc->b]->value->xComplex.value = ComplexOperand( vmc->a );
		}OPNEXT() OPCASE( MOVE_II ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( ADD_III ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a ) + IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( SUB_III ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a ) - IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( MUL_III ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a ) * IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( DIV_III ){
			inum = IntegerOperand( vmc->b );
			if( inum ==0 ) goto RaiseErrorDivByZero;
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a ) / inum;
		}OPNEXT() OPCASE( MOD_III ){
			inum = IntegerOperand( vmc->b );
			if( inum ==0 ) goto RaiseErrorDivByZero;
			IntegerOperand( vmc->c )=(daoint)IntegerOperand( vmc->a ) % inum;
		}OPNEXT() OPCASE( POW_III ){
			IntegerOperand( vmc->c ) = pow( IntegerOperand( vmc->a ), IntegerOperand( vmc->b ) );
		}OPNEXT() OPCASE( AND_III ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a )
				? IntegerOperand( vmc->b ) : IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( OR_III ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a )
				? IntegerOperand( vmc->a ) : IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( NOT_I ){
			IntegerOperand( vmc->c ) = ! IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( MINUS_I ){
			IntegerOperand( vmc->c ) = - IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( LT_III ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a ) < IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( LE_III ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a ) <= IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( EQ_III ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a ) == IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( NE_III ){
			IntegerOperand( vmc->c ) = IntegerOperand( vmc->a ) != IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( BITAND_III ){
			IntegerOperand( vmc->c ) = (daoint)IntegerOperand( vmc->a ) & (daoint)IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( BITOR_III ){
			IntegerOperand( vmc->c ) = (daoint)IntegerOperand( vmc->a ) | (daoint)IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( BITXOR_III ){
			IntegerOperand( vmc->c ) = (daoint)IntegerOperand( vmc->a ) ^ (daoint)IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( BITLFT_III ){
			IntegerOperand( vmc->c ) = (daoint)IntegerOperand( vmc->a ) << (daoint)IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( BITRIT_III ){
			IntegerOperand( vmc->c ) = (daoint)IntegerOperand( vmc->a ) >> (daoint)IntegerOperand( vmc->b );
		}OPNEXT() OPCASE( TILDE_I ){
			IntegerOperand( vmc->c ) = ~ (daoint) IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( TILDE_C ){
			vA = locVars[ vmc->a ];
			vC = locVars[ vmc->c ];
			vC->xComplex.value.real =   vA->xComplex.value.real;
			vC->xComplex.value.imag = - vA->xComplex.value.imag;
		}OPNEXT() OPCASE( MOVE_FF ){
			FloatOperand( vmc->c ) = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( ADD_FFF ){
			FloatOperand( vmc->c ) = FloatOperand( vmc->a ) + FloatOperand( vmc->b );
		}OPNEXT() OPCASE( SUB_FFF ){
			FloatOperand( vmc->c ) = FloatOperand( vmc->a ) - FloatOperand( vmc->b );
		}OPNEXT() OPCASE( MUL_FFF ){
			FloatOperand( vmc->c ) = FloatOperand( vmc->a ) * FloatOperand( vmc->b );
		}OPNEXT() OPCASE( DIV_FFF ){
			FloatOperand( vmc->c ) = FloatOperand( vmc->a ) / FloatOperand( vmc->b );
		}OPNEXT() OPCASE( MOD_FFF ){
			fnum = FloatOperand( vmc->b );
			if( fnum == 0.0 ) goto RaiseErrorDivByZero;
			inum = (daoint)(FloatOperand( vmc->a ) / fnum);
			FloatOperand( vmc->c ) = FloatOperand( vmc->a ) - inum * fnum;
		}OPNEXT() OPCASE( POW_FFF ){
			FloatOperand( vmc->c ) = powf( FloatOperand( vmc->a ), FloatOperand( vmc->b ) );
		}OPNEXT() OPCASE( AND_FFF ){
			fnum = FloatOperand( vmc->a );
			FloatOperand( vmc->c ) = fnum ? FloatOperand( vmc->b ) : fnum;
		}OPNEXT() OPCASE( OR_FFF ){
			fnum = FloatOperand( vmc->a );
			FloatOperand( vmc->c ) = fnum ? fnum : FloatOperand( vmc->b );
		}OPNEXT() OPCASE( NOT_F ){
			FloatOperand( vmc->c ) = ! FloatOperand( vmc->a );
		}OPNEXT() OPCASE( MINUS_F ){
			FloatOperand( vmc->c ) = - FloatOperand( vmc->a );
		}OPNEXT() OPCASE( LT_IFF ){
			IntegerOperand( vmc->c ) = FloatOperand( vmc->a ) < FloatOperand( vmc->b );
		}OPNEXT() OPCASE( LE_IFF ){
			IntegerOperand( vmc->c ) = FloatOperand( vmc->a ) <= FloatOperand( vmc->b );
		}OPNEXT() OPCASE( EQ_IFF ){
			IntegerOperand( vmc->c ) = FloatOperand( vmc->a ) == FloatOperand( vmc->b );
		}OPNEXT() OPCASE( NE_IFF ){
			IntegerOperand( vmc->c ) = FloatOperand( vmc->a ) != FloatOperand( vmc->b );
		}OPNEXT() OPCASE( MOVE_DD ){
			DoubleOperand( vmc->c ) = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( ADD_DDD ){
			DoubleOperand( vmc->c ) = DoubleOperand( vmc->a ) + DoubleOperand( vmc->b );
		}OPNEXT() OPCASE( SUB_DDD ){
			DoubleOperand( vmc->c ) = DoubleOperand( vmc->a ) - DoubleOperand( vmc->b );
		}OPNEXT() OPCASE( MUL_DDD ){
			DoubleOperand( vmc->c ) = DoubleOperand( vmc->a ) * DoubleOperand( vmc->b );
		}OPNEXT() OPCASE( DIV_DDD ){
			DoubleOperand( vmc->c ) = DoubleOperand( vmc->a ) / DoubleOperand( vmc->b );
		}OPNEXT() OPCASE( MOD_DDD ){
			dnum = DoubleOperand( vmc->b );
			if( dnum == 0.0 ) goto RaiseErrorDivByZero;
			inum = (daoint)(DoubleOperand( vmc->a ) / dnum);
			DoubleOperand( vmc->c ) = DoubleOperand( vmc->a ) - inum * dnum;
		}OPNEXT() OPCASE( POW_DDD ){
			DoubleOperand( vmc->c ) = pow( DoubleOperand( vmc->a ), DoubleOperand( vmc->b ) );
		}OPNEXT() OPCASE( AND_DDD ){
			dnum = DoubleOperand( vmc->a );
			DoubleOperand( vmc->c ) = dnum ? DoubleOperand( vmc->b ) : dnum;
		}OPNEXT() OPCASE( OR_DDD ){
			dnum = DoubleOperand( vmc->a );
			DoubleOperand( vmc->c ) = dnum ? dnum : DoubleOperand( vmc->b );
		}OPNEXT() OPCASE( NOT_D ){
			DoubleOperand( vmc->c ) = ! DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( MINUS_D ){
			DoubleOperand( vmc->c ) = - DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( LT_IDD ){
			IntegerOperand( vmc->c ) = DoubleOperand( vmc->a ) < DoubleOperand( vmc->b );
		}OPNEXT() OPCASE( LE_IDD ){
			IntegerOperand( vmc->c ) = DoubleOperand( vmc->a ) <= DoubleOperand( vmc->b );
		}OPNEXT() OPCASE( EQ_IDD ){
			IntegerOperand( vmc->c ) = DoubleOperand( vmc->a ) == DoubleOperand( vmc->b );
		}OPNEXT() OPCASE( NE_IDD ){
			IntegerOperand( vmc->c ) = DoubleOperand( vmc->a ) != DoubleOperand( vmc->b );
		}OPNEXT() OPCASE( ADD_SSS ){
			vA = locVars[ vmc->a ];  vB = locVars[ vmc->b ];
			vC = locVars[ vmc->c ];
			if( vmc->a == vmc->c ){
				DString_Append( vA->xString.data, vB->xString.data );
			}else if( vmc->b == vmc->c ){
				DString_Insert( vB->xString.data, vA->xString.data, 0, 0, 0 );
			}else{
				DString_Assign( vC->xString.data, vA->xString.data );
				DString_Append( vC->xString.data, vB->xString.data );
			}
		}OPNEXT() OPCASE( LT_ISS ){
			vA = locVars[ vmc->a ];  vB = locVars[ vmc->b ];
			IntegerOperand( vmc->c ) = DString_Compare( vA->xString.data, vB->xString.data ) <0;
		}OPNEXT() OPCASE( LE_ISS ){
			vA = locVars[ vmc->a ];  vB = locVars[ vmc->b ];
			IntegerOperand( vmc->c ) = DString_Compare( vA->xString.data, vB->xString.data ) <=0;
		}OPNEXT() OPCASE( EQ_ISS ){
			vA = locVars[ vmc->a ];  vB = locVars[ vmc->b ];
			IntegerOperand( vmc->c ) = DString_Compare( vA->xString.data, vB->xString.data ) ==0;
		}OPNEXT() OPCASE( NE_ISS ){
			vA = locVars[ vmc->a ];  vB = locVars[ vmc->b ];
			IntegerOperand( vmc->c ) = DString_Compare( vA->xString.data, vB->xString.data ) !=0;
		}OPNEXT() OPCASE( MOVE_IF ){
			IntegerOperand( vmc->c ) = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( MOVE_ID ){
			IntegerOperand( vmc->c ) = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( MOVE_FI ){
			FloatOperand( vmc->c ) = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( MOVE_FD ){
			FloatOperand( vmc->c ) = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( MOVE_DI ){
			DoubleOperand( vmc->c ) = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( MOVE_DF ){
			DoubleOperand( vmc->c ) = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( MOVE_CI ){
			locVars[ vmc->c ]->xComplex.value.real = locVars[ vmc->a ]->xInteger.value;
			locVars[ vmc->c ]->xComplex.value.imag = 0.0;
		}OPNEXT() OPCASE( MOVE_CF ){
			locVars[ vmc->c ]->xComplex.value.real = locVars[ vmc->a ]->xFloat.value;
			locVars[ vmc->c ]->xComplex.value.imag = 0.0;
		}OPNEXT() OPCASE( MOVE_CD ){
			locVars[ vmc->c ]->xComplex.value.real = locVars[ vmc->a ]->xDouble.value;
			locVars[ vmc->c ]->xComplex.value.imag = 0.0;
		}OPNEXT() OPCASE( MOVE_CC ){
			ComplexOperand( vmc->c ) = ComplexOperand( vmc->a );
		}OPNEXT() OPCASE( MOVE_SS ){
			DString_Assign( locVars[ vmc->c ]->xString.data, locVars[ vmc->a ]->xString.data );
		}OPNEXT() OPCASE( MOVE_PP ){
			if( locVars[ vmc->a ] == NULL ) goto RaiseErrorNullObject;
			value = locVars[ vmc->a ];
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( MOVE_XX ){
			if( locVars[ vmc->a ] == NULL ) goto RaiseErrorNullObject;
			DaoValue_CopyX( locVars[ vmc->a ], locVars + vmc->c, self->cache );
		}OPNEXT() OPCASE( MINUS_C ){
			acom = ComplexOperand( vmc->a );
			vC = locVars[ vmc->c ];
			vC->xComplex.value.real = - acom.real;
			vC->xComplex.value.imag = - acom.imag;
		}OPNEXT() OPCASE( ADD_CCC ){
			acom = ComplexOperand( vmc->a );  bcom = ComplexOperand( vmc->b );
			vC = locVars[ vmc->c ];
			vC->xComplex.value.real = acom.real + bcom.real;
			vC->xComplex.value.imag = acom.imag + bcom.imag;
		}OPNEXT() OPCASE( SUB_CCC ){
			acom = ComplexOperand( vmc->a );  bcom = ComplexOperand( vmc->b );
			vC = locVars[ vmc->c ];
			vC->xComplex.value.real = acom.real - bcom.real;
			vC->xComplex.value.imag = acom.imag - bcom.imag;
		}OPNEXT() OPCASE( MUL_CCC ){
			acom = ComplexOperand( vmc->a );  bcom = ComplexOperand( vmc->b );
			vC = locVars[ vmc->c ];
			vC->xComplex.value.real = acom.real * bcom.real - acom.imag * bcom.imag;
			vC->xComplex.value.imag = acom.real * bcom.imag + acom.imag * bcom.real;
		}OPNEXT() OPCASE( DIV_CCC ){
			acom = ComplexOperand( vmc->a );  bcom = ComplexOperand( vmc->b );
			vC = locVars[ vmc->c ];
			dnum = bcom.real * bcom.real + bcom.imag * bcom.imag;
			vC->xComplex.value.real = (acom.real*bcom.real + acom.imag*bcom.imag) / dnum;
			vC->xComplex.value.imag = (acom.imag*bcom.real - acom.real*bcom.imag) / dnum;
		}OPNEXT() OPCASE( EQ_ICC ){
			complex16 *ca = & locVars[vmc->a]->xComplex.value;
			complex16 *cb = & locVars[vmc->b]->xComplex.value;
			IntegerOperand( vmc->c ) = ca->real == cb->real && ca->imag == cb->imag;
		}OPNEXT() OPCASE( NE_ICC ){
			complex16 *ca = & locVars[vmc->a]->xComplex.value;
			complex16 *cb = & locVars[vmc->b]->xComplex.value;
			IntegerOperand( vmc->c ) = ca->real != cb->real || ca->imag != cb->imag;
		}OPNEXT() OPCASE( GETI_SI ){
			str = locVars[ vmc->a ]->xString.data;
			id = IntegerOperand( vmc->b );
			if( id <0 ) id += str->size;
			if( id <0 || id >= str->size ) goto RaiseErrorIndexOutOfRange;
			if( str->mbs ){
				IntegerOperand( vmc->c ) = str->mbs[id];
			}else{
				IntegerOperand( vmc->c ) = str->wcs[id];
			}
		}OPNEXT() OPCASE( SETI_SII ){
			str = locVars[ vmc->c ]->xString.data;
			id = IntegerOperand( vmc->b );
			inum = IntegerOperand( vmc->a );
			if( id <0 ) id += str->size;
			if( id <0 || id >= str->size ) goto RaiseErrorIndexOutOfRange;
			DString_Detach( str, str->size );
			if( str->mbs ){
				str->mbs[id] = inum;
			}else{
				str->wcs[id] = inum;
			}
		}OPNEXT() OPCASE( GETI_LI ){
			list = & locVars[ vmc->a ]->xList;
			id = IntegerOperand( vmc->b );
			if( id <0 ) id += list->items.size;
			if( id <0 || id >= list->items.size ) goto RaiseErrorIndexOutOfRange;
			/* All GETX instructions assume the C regisgter is an intermediate register! */
			/* So no type checking is necessary here! */
			value = list->items.items.pValue[id];
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( SETI_LI ){
			list = & locVars[ vmc->c ]->xList;
			id = IntegerOperand( vmc->b );
			if( id <0 ) id += list->items.size;
			if( id <0 || id >= list->items.size ) goto RaiseErrorIndexOutOfRange;
			DaoValue_Copy( locVars[ vmc->a ], list->items.items.pValue + id );
		}OPNEXT()
		OPCASE( GETI_LII )
			OPCASE( GETI_LFI )
			OPCASE( GETI_LDI )
			OPCASE( GETI_LCI )
			OPCASE( GETI_LSI ){
				list = & locVars[ vmc->a ]->xList;
				id = IntegerOperand( vmc->b );
				if( id <0 ) id += list->items.size;
				if( id <0 || id >= list->items.size ) goto RaiseErrorIndexOutOfRange;
				vA = list->items.items.pValue[id];
				switch( vmc->code ){
				case DVM_GETI_LSI :
					GC_ShiftRC( vA, locVars[ vmc->c ] );
					locVars[ vmc->c ] = vA;
					break;
				case DVM_GETI_LII : locVars[ vmc->c ]->xInteger.value = vA->xInteger.value; break;
				case DVM_GETI_LFI : locVars[ vmc->c ]->xFloat.value = vA->xFloat.value; break;
				case DVM_GETI_LDI : locVars[ vmc->c ]->xDouble.value = vA->xDouble.value; break;
				case DVM_GETI_LCI : locVars[ vmc->c ]->xComplex.value = vA->xComplex.value; break;
				}
			}OPNEXT()
		OPCASE( SETI_LIII ){
			list = & locVars[ vmc->c ]->xList;
			id = IntegerOperand( vmc->b );
			if( id <0 ) id += list->items.size;
			if( id <0 || id >= list->items.size ) goto RaiseErrorIndexOutOfRange;
			list->items.items.pValue[id]->xInteger.value = locVars[ vmc->a ]->xInteger.value;
		}OPNEXT() OPCASE( SETI_LFIF ){
			list = & locVars[ vmc->c ]->xList;
			id = IntegerOperand( vmc->b );
			if( id <0 ) id += list->items.size;
			if( id <0 || id >= list->items.size ) goto RaiseErrorIndexOutOfRange;
			list->items.items.pValue[id]->xFloat.value = locVars[ vmc->a ]->xFloat.value;
		}OPNEXT() OPCASE( SETI_LDID ){
			list = & locVars[ vmc->c ]->xList;
			id = IntegerOperand( vmc->b );
			if( id <0 ) id += list->items.size;
			if( id <0 || id >= list->items.size ) goto RaiseErrorIndexOutOfRange;
			list->items.items.pValue[id]->xDouble.value = locVars[ vmc->a ]->xDouble.value;
		}OPNEXT() OPCASE( SETI_LCIC ){
			list = & locVars[ vmc->c ]->xList;
			id = IntegerOperand( vmc->b );
			if( id <0 ) id += list->items.size;
			if( id <0 || id >= list->items.size ) goto RaiseErrorIndexOutOfRange;
			list->items.items.pValue[id]->xComplex.value = locVars[ vmc->a ]->xComplex.value;
		}OPNEXT() OPCASE( SETI_LSIS ){
			list = & locVars[ vmc->c ]->xList;
			vA = locVars[ vmc->a ];
			id = IntegerOperand( vmc->b );
			if( id <0 ) id += list->items.size;
			if( id <0 || id >= list->items.size ) goto RaiseErrorIndexOutOfRange;
			DString_Assign( list->items.items.pValue[id]->xString.data, vA->xString.data );
		}OPNEXT()
#ifdef DAO_WITH_NUMARRAY
		OPCASE( GETI_AII ) OPCASE( GETI_AFI ) OPCASE( GETI_ADI ) OPCASE( GETI_ACI ){
			array = & locVars[ vmc->a ]->xArray;
			id = IntegerOperand( vmc->b );
			if( array->original && DaoArray_Sliced( array ) == 0 ) goto RaiseErrorSlicing;
			if( id <0 ) id += array->size;
			if( id <0 || id >= array->size ) goto RaiseErrorIndexOutOfRange;
			switch( vmc->code ){
			case DVM_GETI_AII : IntegerOperand( vmc->c ) = array->data.i[id]; break;
			case DVM_GETI_AFI : FloatOperand( vmc->c ) = array->data.f[id]; break;
			case DVM_GETI_ADI : DoubleOperand( vmc->c ) = array->data.d[id]; break;
			case DVM_GETI_ACI : locVars[ vmc->c ]->xComplex.value = array->data.c[id]; break;
			}

		}OPNEXT() OPCASE(SETI_AIII) OPCASE(SETI_AFIF) OPCASE(SETI_ADID) OPCASE(SETI_ACIC){
			array = & locVars[ vmc->c ]->xArray;
			id = IntegerOperand( vmc->b );
			if( array->original && DaoArray_Sliced( array ) == 0 ) goto RaiseErrorSlicing;
			if( id <0 ) id += array->size;
			if( id <0 || id >= array->size ) goto RaiseErrorIndexOutOfRange;
			switch( vmc->code ){
			case DVM_SETI_AIII : array->data.i[id] = locVars[ vmc->a ]->xInteger.value; break;
			case DVM_SETI_AFIF : array->data.f[id] = locVars[ vmc->a ]->xFloat.value; break;
			case DVM_SETI_ADID : array->data.d[id] = locVars[ vmc->a ]->xDouble.value; break;
			case DVM_SETI_ACIC : array->data.c[ id ] = locVars[ vmc->a ]->xComplex.value; break;
			}

		}OPNEXT() OPCASE(GETMI_AII) OPCASE(GETMI_AFI) OPCASE(GETMI_ADI) OPCASE(GETMI_ACI){
			array = & locVars[ vmc->a ]->xArray;
			if( array->original && DaoArray_Sliced( array ) == 0 ) goto RaiseErrorSlicing;
			id = DaoArray_ComputeIndex( array, locVars + vmc->a + 1, vmc->b );
			if( id < 0 ) goto RaiseErrorIndexOutOfRange;
			switch( vmc->code ){
			case DVM_GETMI_AII: locVars[ vmc->c ]->xInteger.value = array->data.i[ id ]; break;
			case DVM_GETMI_AFI: locVars[ vmc->c ]->xFloat.value = array->data.f[ id ]; break;
			case DVM_GETMI_ADI: locVars[ vmc->c ]->xDouble.value = array->data.d[ id ]; break;
			case DVM_GETMI_ACI: locVars[ vmc->c ]->xComplex.value = array->data.c[ id ]; break;
			}

		}OPNEXT() OPCASE(SETMI_AIII) OPCASE(SETMI_AFIF) OPCASE(SETMI_ADID) OPCASE(SETMI_ACIC){
			array = & locVars[ vmc->c ]->xArray;
			if( array->original && DaoArray_Sliced( array ) == 0 ) goto RaiseErrorSlicing;
			id = DaoArray_ComputeIndex( array, locVars + vmc->c + 1, vmc->b  );
			if( id < 0 ) goto RaiseErrorIndexOutOfRange;
			switch( vmc->code ){
			case DVM_SETMI_AIII: array->data.i[ id ] = locVars[ vmc->a ]->xInteger.value; break;
			case DVM_SETMI_AFIF: array->data.f[ id ] = locVars[ vmc->a ]->xFloat.value; break;
			case DVM_SETMI_ADID: array->data.d[ id ] = locVars[ vmc->a ]->xDouble.value; break;
			case DVM_SETMI_ACIC: array->data.c[ id ] = locVars[ vmc->a ]->xComplex.value; break;
			}
		}OPNEXT()
#else
		OPCASE( GETI_AII ) OPCASE( GETI_AFI ) OPCASE( GETI_ADI ) OPCASE( GETI_ACI )
			OPCASE( SETI_AIII ) OPCASE( SETI_AFIF ) OPCASE( SETI_ADID ) OPCASE( SETI_ACIC )
			OPCASE( GETMI_AII ) OPCASE( GETMI_AFI )
			OPCASE( GETMI_ADI ) OPCASE( GETMI_ACI )
			OPCASE( SETMI_AIII ) OPCASE( SETMI_AFIF )
			OPCASE( SETMI_ADID ) OPCASE( SETMI_ACIC )
			{
				self->activeCode = vmc;
				DaoProcess_RaiseException( self, DAO_ERROR, "numeric array is disabled" );
			}OPNEXT()
#endif
		OPCASE( GETI_TI ){
			tuple = & locVars[ vmc->a ]->xTuple;
			id = IntegerOperand( vmc->b );
			if( id <0 || id >= tuple->size ) goto RaiseErrorIndexOutOfRange;
			value = tuple->items[id];
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( SETI_TI ){
			tuple = & locVars[ vmc->c ]->xTuple;
			id = IntegerOperand( vmc->b );
			abtp = NULL;
			if( id <0 || id >= tuple->size ) goto RaiseErrorIndexOutOfRange;
			abtp = tuple->unitype->nested->items.pType[id];
			if( abtp->tid == DAO_PAR_NAMED ) abtp = & abtp->aux->xType;
			if( DaoProcess_Move( self, locVars[vmc->a], tuple->items + id, abtp ) ==0 )
				goto CheckException;
		}OPNEXT() OPCASE( GETF_TI ){
			/* Do not get reference here!
			 * Getting reference is always more expensive due to reference counting.
			 * The compiler always generates SETX, if element modification is done
			 * through index or field accessing: A[B] += C, A.B += C. */
			tuple = & locVars[ vmc->a ]->xTuple;
			locVars[ vmc->c ]->xInteger.value = tuple->items[ vmc->b ]->xInteger.value;
		}OPNEXT() OPCASE( GETF_TF ){
			tuple = & locVars[ vmc->a ]->xTuple;
			locVars[ vmc->c ]->xFloat.value = tuple->items[ vmc->b ]->xFloat.value;
		}OPNEXT() OPCASE( GETF_TD ){
			tuple = & locVars[ vmc->a ]->xTuple;
			locVars[ vmc->c ]->xDouble.value = tuple->items[ vmc->b ]->xDouble.value;
		}OPNEXT() OPCASE( GETF_TC ){
			tuple = & locVars[ vmc->a ]->xTuple;
			locVars[ vmc->c ]->xComplex.value = tuple->items[ vmc->b ]->xComplex.value;
		}OPNEXT() OPCASE( GETF_TX ){
			tuple = & locVars[ vmc->a ]->xTuple;
			value = tuple->items[ vmc->b ];
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( SETF_TII ){
			tuple = & locVars[ vmc->c ]->xTuple;
			tuple->items[ vmc->b ]->xInteger.value = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_TFF ){
			tuple = & locVars[ vmc->c ]->xTuple;
			tuple->items[ vmc->b ]->xFloat.value = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_TDD ){
			tuple = & locVars[ vmc->c ]->xTuple;
			tuple->items[ vmc->b ]->xDouble.value = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_TCC ){
			tuple = & locVars[ vmc->c ]->xTuple;
			tuple->items[ vmc->b ]->xComplex.value = ComplexOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_TSS ){
			tuple = & locVars[ vmc->c ]->xTuple;
			vA = locVars[ vmc->a ];
			DString_Assign( tuple->items[ vmc->b ]->xString.data, vA->xString.data );
		}OPNEXT() OPCASE( SETF_TPP ){
			tuple = & locVars[ vmc->c ]->xTuple;
			value = locVars[ vmc->a ];
			vC2 = tuple->items + vmc->b;
			GC_ShiftRC( value, *vC2 );
			*vC2 = value;
		}OPNEXT() OPCASE( SETF_TXX ){
			tuple = & locVars[ vmc->c ]->xTuple;
			DaoValue_Copy( locVars[ vmc->a ], tuple->items + vmc->b );
		}OPNEXT() OPCASE( GETF_CX ){
			double *RI = (double*)(complex16*) & locVars[ vmc->a ]->xComplex.value;
			locVars[ vmc->c ]->xDouble.value = RI[ vmc->b ];
		}OPNEXT() OPCASE( SETF_CX ){
			double *RI = (double*)(complex16*) & locVars[ vmc->c ]->xComplex.value;
			RI[ vmc->b ] = locVars[ vmc->a ]->xDouble.value;
		}OPNEXT() OPCASE( GETF_KC ){
			value = locVars[ vmc->a ]->xClass.constants->items.pConst[ vmc->b ]->value;
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETF_KG ){
			value = locVars[ vmc->a ]->xClass.variables->items.pVar[ vmc->b ]->value;
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETF_OC ){
			value = locVars[ vmc->a ]->xObject.defClass->constants->items.pConst[ vmc->b ]->value;
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETF_OG ){
			value = locVars[ vmc->a ]->xObject.defClass->variables->items.pVar[ vmc->b ]->value;
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETF_OV ){
			object = & locVars[ vmc->a ]->xObject;
			if( object == & object->defClass->objType->value->xObject ) goto AccessDefault;
			value = object->objValues[ vmc->b ];
			GC_ShiftRC( value, locVars[ vmc->c ] );
			locVars[ vmc->c ] = value;
		}OPNEXT() OPCASE( GETF_KCI ){
			value = locVars[ vmc->a ]->xClass.constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xInteger.value = value->xInteger.value;
		}OPNEXT() OPCASE( GETF_KCF ){
			value = locVars[ vmc->a ]->xClass.constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xFloat.value = value->xFloat.value;
		}OPNEXT() OPCASE( GETF_KCD ){
			value = locVars[ vmc->a ]->xClass.constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xDouble.value = value->xDouble.value;
		}OPNEXT() OPCASE( GETF_KCC ){
			value = locVars[ vmc->a ]->xClass.constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xComplex.value = value->xComplex.value;
		}OPNEXT() OPCASE( GETF_KGI ){
			value = locVars[ vmc->a ]->xClass.variables->items.pVar[ vmc->b ]->value;
			locVars[ vmc->c ]->xInteger.value = value->xInteger.value;
		}OPNEXT() OPCASE( GETF_KGF ){
			value = locVars[ vmc->a ]->xClass.variables->items.pVar[ vmc->b ]->value;
			locVars[ vmc->c ]->xFloat.value = value->xFloat.value;
		}OPNEXT() OPCASE( GETF_KGD ){
			value = locVars[ vmc->a ]->xClass.variables->items.pVar[ vmc->b ]->value;
			locVars[ vmc->c ]->xDouble.value = value->xDouble.value;
		}OPNEXT() OPCASE( GETF_KGC ){
			value = locVars[ vmc->a ]->xClass.variables->items.pVar[ vmc->b ]->value;
			locVars[ vmc->c ]->xComplex.value = value->xComplex.value;
		}OPNEXT() OPCASE( GETF_OCI ){
			value = locVars[ vmc->a ]->xObject.defClass->constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xInteger.value = value->xInteger.value;
		}OPNEXT() OPCASE( GETF_OCF ){
			value = locVars[ vmc->a ]->xObject.defClass->constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xFloat.value = value->xFloat.value;
		}OPNEXT() OPCASE( GETF_OCD ){
			value = locVars[ vmc->a ]->xObject.defClass->constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xDouble.value = value->xDouble.value;
		}OPNEXT() OPCASE( GETF_OCC ){
			value = locVars[ vmc->a ]->xObject.defClass->constants->items.pConst[ vmc->b ]->value;
			locVars[ vmc->c ]->xComplex.value = value->xComplex.value;
		}OPNEXT() OPCASE( GETF_OGI ){
			value = locVars[ vmc->a ]->xObject.defClass->variables->items.pVar[ vmc->b ]->value;
			locVars[ vmc->c ]->xInteger.value = value->xInteger.value;
		}OPNEXT() OPCASE( GETF_OGF ){
			value = locVars[ vmc->a ]->xObject.defClass->variables->items.pVar[ vmc->b ]->value;
			locVars[ vmc->c ]->xFloat.value = value->xFloat.value;
		}OPNEXT() OPCASE( GETF_OGD ){
			value = locVars[ vmc->a ]->xObject.defClass->variables->items.pVar[ vmc->b ]->value;
			locVars[ vmc->c ]->xDouble.value = value->xDouble.value;
		}OPNEXT() OPCASE( GETF_OGC ){
			value = locVars[ vmc->a ]->xObject.defClass->variables->items.pVar[ vmc->b ]->value;
			locVars[ vmc->c ]->xComplex.value = value->xComplex.value;
		}OPNEXT() OPCASE( GETF_OVI ){
			value = locVars[ vmc->a ]->xObject.objValues[ vmc->b ];
			locVars[ vmc->c ]->xInteger.value = value->xInteger.value;
		}OPNEXT() OPCASE( GETF_OVF ){
			value = locVars[ vmc->a ]->xObject.objValues[ vmc->b ];
			locVars[ vmc->c ]->xFloat.value = value->xFloat.value;
		}OPNEXT() OPCASE( GETF_OVD ){
			value = locVars[ vmc->a ]->xObject.objValues[ vmc->b ];
			locVars[ vmc->c ]->xDouble.value = value->xDouble.value;
		}OPNEXT() OPCASE( GETF_OVC ){
			value = locVars[ vmc->a ]->xObject.objValues[ vmc->b ];
			locVars[ vmc->c ]->xComplex.value = value->xComplex.value;
		}OPNEXT() OPCASE( SETF_KG ){
			klass = & locVars[ vmc->c ]->xClass;
			DaoValue_Copy( locVars[vmc->a], & klass->variables->items.pVar[vmc->b]->value );
		}OPNEXT() OPCASE( SETF_OG ){
			klass = locVars[ vmc->c ]->xObject.defClass;
			DaoValue_Copy( locVars[vmc->a], & klass->variables->items.pVar[vmc->b]->value );
		}OPNEXT() OPCASE( SETF_OV ){
			object = & locVars[ vmc->c ]->xObject;
			if( object == & object->defClass->objType->value->xObject ) goto AccessDefault;
			DaoValue_Copy( locVars[vmc->a], object->objValues + vmc->b );
		}OPNEXT() OPCASE( SETF_KGII ){
			klass = & locVars[ vmc->c ]->xClass;
			klass->variables->items.pVar[vmc->b]->value->xInteger.value = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_KGFF ){
			klass = & locVars[ vmc->c ]->xClass;
			klass->variables->items.pVar[vmc->b]->value->xFloat.value = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_KGDD ){
			klass = & locVars[ vmc->c ]->xClass;
			klass->variables->items.pVar[vmc->b]->value->xDouble.value = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_KGCC ){
			klass = & locVars[ vmc->c ]->xClass;
			klass->variables->items.pVar[vmc->b]->value->xComplex.value = ComplexOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_OGII ){
			klass = locVars[ vmc->c ]->xObject.defClass;
			klass->variables->items.pVar[vmc->b]->value->xInteger.value = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_OGFF ){
			klass = locVars[ vmc->c ]->xObject.defClass;
			klass->variables->items.pVar[vmc->b]->value->xFloat.value = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_OGDD ){
			klass = locVars[ vmc->c ]->xObject.defClass;
			klass->variables->items.pVar[vmc->b]->value->xDouble.value = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_OGCC ){
			klass = locVars[ vmc->c ]->xObject.defClass;
			klass->variables->items.pVar[vmc->b]->value->xComplex.value = ComplexOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_OVII ){
			object = (DaoObject*) locVars[ vmc->c ];
			if( object == & object->defClass->objType->value->xObject ) goto AccessDefault;
			object->objValues[ vmc->b ]->xInteger.value = IntegerOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_OVFF ){
			object = (DaoObject*) locVars[ vmc->c ];
			if( object == & object->defClass->objType->value->xObject ) goto AccessDefault;
			object->objValues[ vmc->b ]->xFloat.value = FloatOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_OVDD ){
			object = (DaoObject*) locVars[ vmc->c ];
			if( object == & object->defClass->objType->value->xObject ) goto AccessDefault;
			object->objValues[ vmc->b ]->xDouble.value = DoubleOperand( vmc->a );
		}OPNEXT() OPCASE( SETF_OVCC ){
			object = (DaoObject*) locVars[ vmc->c ];
			if( object == & object->defClass->objType->value->xObject ) goto AccessDefault;
			object->objValues[ vmc->b ]->xComplex.value = ComplexOperand( vmc->a );
		}OPNEXT()
		OPCASE( CHECK_ST ){
			vA = locVars[vmc->a];
			locVars[vmc->c]->xInteger.value = vA && vA->type == locVars[vmc->b]->xType.tid;
		}OPNEXT()
		OPCASE( SAFE_GOTO ){
			if( ( self->vmSpace->options & DAO_OPTION_SAFE ) ){
				gotoCount ++;
				if( gotoCount > 1E6 ){
					self->activeCode = vmc;
					DaoProcess_RaiseException( self, DAO_ERROR,
							"too many goto operations for safe running mode." );
					goto CheckException;
				}
			}
			vmc = vmcBase + vmc->b;
		}OPJUMP() OPCASE( TEST_I ){
			vmc = IntegerOperand( vmc->a ) ? vmc+1 : vmcBase+vmc->b;
		}OPJUMP() OPCASE( TEST_F ){
			vmc = FloatOperand( vmc->a ) ? vmc+1 : vmcBase+vmc->b;
		}OPJUMP() OPCASE( TEST_D ){
			vmc = DoubleOperand( vmc->a ) ? vmc+1 : vmcBase+vmc->b;
		}OPJUMP() OPCASE( MATH_I ){
			switch( vmc->a ){
			case DVM_MATH_RAND :
				IntegerOperand(vmc->c) = (int)(IntegerOperand(vmc->b)*DaoProcess_Random(self));
				break;
			case DVM_MATH_CEIL : IntegerOperand(vmc->c) = ceil( IntegerOperand(vmc->b) ); break;
			case DVM_MATH_FLOOR: IntegerOperand(vmc->c) = floor( IntegerOperand(vmc->b) ); break;
			case DVM_MATH_ABS  : IntegerOperand(vmc->c) = abs( IntegerOperand(vmc->b) );  break;
			case DVM_MATH_ACOS : FloatOperand(vmc->c) = acos( IntegerOperand(vmc->b) ); break;
			case DVM_MATH_ASIN : FloatOperand(vmc->c) = asin( IntegerOperand(vmc->b) ); break;
			case DVM_MATH_ATAN : FloatOperand(vmc->c) = atan( IntegerOperand(vmc->b) ); break;
			case DVM_MATH_COS  : FloatOperand(vmc->c) = cos( IntegerOperand(vmc->b) );  break;
			case DVM_MATH_COSH : FloatOperand(vmc->c) = cosh( IntegerOperand(vmc->b) ); break;
			case DVM_MATH_EXP  : FloatOperand(vmc->c) = exp( IntegerOperand(vmc->b) );  break;
			case DVM_MATH_LOG  : FloatOperand(vmc->c) = log( IntegerOperand(vmc->b) );  break;
			case DVM_MATH_SIN  : FloatOperand(vmc->c) = sin( IntegerOperand(vmc->b) );  break;
			case DVM_MATH_SINH : FloatOperand(vmc->c) = sinh( IntegerOperand(vmc->b) ); break;
			case DVM_MATH_SQRT : FloatOperand(vmc->c) = sqrt( IntegerOperand(vmc->b) ); break;
			case DVM_MATH_TAN  : FloatOperand(vmc->c) = tan( IntegerOperand(vmc->b) );  break;
			case DVM_MATH_TANH : FloatOperand(vmc->c) = tanh( IntegerOperand(vmc->b) ); break;
			default : break;
			}
		}OPNEXT() OPCASE( MATH_F ){
			switch( vmc->a ){
			case DVM_MATH_RAND :
				FloatOperand(vmc->c) = FloatOperand(vmc->b) * DaoProcess_Random(self); break;
			case DVM_MATH_CEIL : FloatOperand(vmc->c) = ceil( FloatOperand(vmc->b) ); break;
			case DVM_MATH_FLOOR : FloatOperand(vmc->c) = floor( FloatOperand(vmc->b) ); break;
			case DVM_MATH_ABS  : FloatOperand(vmc->c) = fabs( FloatOperand(vmc->b) );  break;
			case DVM_MATH_ACOS : FloatOperand(vmc->c) = acos( FloatOperand(vmc->b) ); break;
			case DVM_MATH_ASIN : FloatOperand(vmc->c) = asin( FloatOperand(vmc->b) ); break;
			case DVM_MATH_ATAN : FloatOperand(vmc->c) = atan( FloatOperand(vmc->b) ); break;
			case DVM_MATH_COS  : FloatOperand(vmc->c) = cos( FloatOperand(vmc->b) );  break;
			case DVM_MATH_COSH : FloatOperand(vmc->c) = cosh( FloatOperand(vmc->b) ); break;
			case DVM_MATH_EXP  : FloatOperand(vmc->c) = exp( FloatOperand(vmc->b) );  break;
			case DVM_MATH_LOG  : FloatOperand(vmc->c) = log( FloatOperand(vmc->b) );  break;
			case DVM_MATH_SIN  : FloatOperand(vmc->c) = sin( FloatOperand(vmc->b) );  break;
			case DVM_MATH_SINH : FloatOperand(vmc->c) = sinh( FloatOperand(vmc->b) ); break;
			case DVM_MATH_SQRT : FloatOperand(vmc->c) = sqrt( FloatOperand(vmc->b) ); break;
			case DVM_MATH_TAN  : FloatOperand(vmc->c) = tan( FloatOperand(vmc->b) );  break;
			case DVM_MATH_TANH : FloatOperand(vmc->c) = tanh( FloatOperand(vmc->b) ); break;
			default : break;
			}
		}OPNEXT() OPCASE( MATH_D ){
			switch( vmc->a ){
			case DVM_MATH_RAND :
				DoubleOperand(vmc->c) = DoubleOperand(vmc->b) * DaoProcess_Random(self); break;
			case DVM_MATH_CEIL : DoubleOperand(vmc->c) = ceil( DoubleOperand(vmc->b) ); break;
			case DVM_MATH_FLOOR : DoubleOperand(vmc->c) = floor( DoubleOperand(vmc->b) ); break;
			case DVM_MATH_ABS  : DoubleOperand(vmc->c) = fabs( DoubleOperand(vmc->b) );  break;
			case DVM_MATH_ACOS : DoubleOperand(vmc->c) = acos( DoubleOperand(vmc->b) ); break;
			case DVM_MATH_ASIN : DoubleOperand(vmc->c) = asin( DoubleOperand(vmc->b) ); break;
			case DVM_MATH_ATAN : DoubleOperand(vmc->c) = atan( DoubleOperand(vmc->b) ); break;
			case DVM_MATH_COS  : DoubleOperand(vmc->c) = cos( DoubleOperand(vmc->b) );  break;
			case DVM_MATH_COSH : DoubleOperand(vmc->c) = cosh( DoubleOperand(vmc->b) ); break;
			case DVM_MATH_EXP  : DoubleOperand(vmc->c) = exp( DoubleOperand(vmc->b) );  break;
			case DVM_MATH_LOG  : DoubleOperand(vmc->c) = log( DoubleOperand(vmc->b) );  break;
			case DVM_MATH_SIN  : DoubleOperand(vmc->c) = sin( DoubleOperand(vmc->b) );  break;
			case DVM_MATH_SINH : DoubleOperand(vmc->c) = sinh( DoubleOperand(vmc->b) ); break;
			case DVM_MATH_SQRT : DoubleOperand(vmc->c) = sqrt( DoubleOperand(vmc->b) ); break;
			case DVM_MATH_TAN  : DoubleOperand(vmc->c) = tan( DoubleOperand(vmc->b) );  break;
			case DVM_MATH_TANH : DoubleOperand(vmc->c) = tanh( DoubleOperand(vmc->b) ); break;
			default : break;
			}
		}OPNEXT()
		OPDEFAULT()
		{
			goto CheckException;
RaiseErrorIndexOutOfRange:
			self->activeCode = vmc;
			DaoProcess_RaiseException( self, DAO_ERROR_INDEX_OUTOFRANGE, "" );
			goto CheckException;
RaiseErrorSlicing:
			self->activeCode = vmc;
			DaoProcess_RaiseException( self, DAO_ERROR_INDEX, "slicing" );
			goto CheckException;
RaiseErrorDivByZero:
			self->activeCode = vmc;
			DaoProcess_RaiseException( self, DAO_ERROR_FLOAT_DIVBYZERO, "" );
			goto CheckException;
RaiseErrorInvalidOperation:
			self->activeCode = vmc;
			DaoProcess_RaiseException( self, DAO_ERROR, "invalid operation" );
			goto CheckException;
ModifyConstant:
			self->activeCode = vmc;
			DaoProcess_RaiseException( self, DAO_ERROR, "attempt to modify a constant" );
			goto CheckException;
AccessDefault:
			self->activeCode = vmc;
			DaoProcess_RaiseException( self, DAO_ERROR, "cannot modify default class instance" );
			goto CheckException;
RaiseErrorNullObject:
			self->activeCode = vmc;
			DaoProcess_RaiseException( self, DAO_ERROR, "operate on none object" );
			goto CheckException;
CheckException:

			locVars = self->activeValues;
			if( self->stopit | vmSpace->stopit ) goto FinishProcess;
			//XXX if( invokehost ) handler->InvokeHost( handler, topCtx );
			if( self->exceptions->size > exceptCount ){
				goto FinishCall;
			}else if( self->status == DAO_PROCESS_STACKED ){
				goto CallEntry;
			}else if( self->status == DAO_PROCESS_SUSPENDED ){
				self->topFrame->entry = (short)(vmc - vmcBase);
				goto ReturnFalse;
			}else if( self->status == DAO_PROCESS_ABORTED ){
				goto FinishProcess;
			}
			OPNEXT()
		}
	}OPEND()

FinishCall:

	if( self->defers->size > self->topFrame->deferBase ){
		self->topFrame->state |= DVM_FRAME_FINISHED;
		DaoProcess_PushDefers( self, NULL );
		goto CallEntry;
	}

	if( (routine->attribs & DAO_ROUT_PASSRET) ){ /* Update the modified return: */
		if( self->topFrame->prev->deferBase < self->topFrame->deferBase ){
			/* This deferred closure was created by its caller: */
			daoint returning = self->topFrame->prev->returning;
			DaoValue **dest = self->stackValues;
			DaoType *type = NULL;

			if( returning != (ushort_t)-1 ){
				DaoStackFrame *lastframe = self->topFrame->prev->prev;
				type = lastframe->routine->body->regType->items.pType[ returning ];
				dest = self->stackValues + lastframe->stackBase + returning;
			}
			DaoValue_Move( routine->body->svariables->items.pVar[0]->value, dest, type );
		}
	}

	if( self->topFrame->state & DVM_FRAME_KEEP ){
		self->topFrame->state &= ~DVM_FRAME_RUNNING;
		self->status = DAO_PROCESS_FINISHED;
		if( self->exceptions->size > exceptCount0 ) goto AbortProcess;
		goto ReturnTrue;
	}
	DaoProcess_PopFrame( self );
	DaoGC_TryInvoke();
	goto CallEntry;

CallNotPermitted:
	/* DaoProcess_PopFrame( self ); cannot popframe, it may be tail-call optimized! */
	DaoProcess_RaiseException( self, DAO_ERROR, "CallNotPermitted" );

FinishProcess:

	if( self->exceptions->size ) DaoProcess_PrintException( self, NULL, 1 );
	DaoProcess_PopFrames( self, rollback );
	/*if( eventHandler ) eventHandler->mainRoutineExit(); */

AbortProcess:
	self->status = DAO_PROCESS_ABORTED;

ReturnFalse:
#ifdef DAO_WITH_CONCURRENT
	/*
	// active==0:       if the process is started outside of the tasklet pool;
	// self->active!=0: if the process has been added to the active process list;
	// Now it must be manually removed from the active process list:
	*/
	if( active == 0 && self->active ) DaoCallServer_MarkActiveProcess( self, 0 );
#endif
	DaoDataCache_Release( self->cache );
	self->cache = NULL;
	DaoGC_TryInvoke();
	return 0;

ReturnTrue:
	if( self->topFrame == self->firstFrame && self == vmSpace->mainProcess ){
		print = (vmSpace->options & DAO_OPTION_INTERUN) && (here->options & DAO_NS_AUTO_GLOBAL);
		if( (print || vmSpace->evalCmdline) && self->stackValues[0] ){
			/* Need one extra frame to ensure this part is not executed again,
			// in case that DaoValue_Print() will invoke some methods: */
			DaoProcess_PushFrame( self, 0 );
			DaoStream_WriteMBS( vmSpace->stdioStream, "= " );
			DaoValue_Print( self->stackValues[0], self, vmSpace->stdioStream, NULL );
			DaoStream_WriteNewLine( vmSpace->stdioStream );
			DaoProcess_PopFrame( self );
		}
	}
#ifdef DAO_WITH_CONCURRENT
	if( active == 0 && self->active ) DaoCallServer_MarkActiveProcess( self, 0 );
#endif
	DaoDataCache_Release( self->cache );
	self->cache = NULL;
	DaoGC_TryInvoke();
	return 1;
}
DaoVmCode* DaoProcess_DoSwitch( DaoProcess *self, DaoVmCode *vmc )
{
	DaoVmCode *mid;
	DaoValue **cst = self->activeRoutine->routConsts->items.items.pValue;
	DaoValue *opa = self->activeValues[ vmc->a ];
	int first, last, cmp, id;
	daoint min, max;

	if( vmc->c ==0 ) return self->topFrame->codes + vmc->b;
	if( vmc[1].c == DAO_CASE_TABLE ){
		if( opa->type == DAO_INTEGER ){
			min = cst[ vmc[1].a ]->xInteger.value;
			max = cst[ vmc[vmc->c].a ]->xInteger.value;
			if( opa->xInteger.value >= min && opa->xInteger.value <= max )
				return self->topFrame->codes + vmc[ opa->xInteger.value - min + 1 ].b;
		}else if( opa->type== DAO_ENUM ){
			min = cst[ vmc[1].a ]->xEnum.value;
			max = cst[ vmc[vmc->c].a ]->xEnum.value;
			if( opa->xEnum.value >= min && opa->xEnum.value <= max )
				return self->topFrame->codes + vmc[ opa->xEnum.value - min + 1 ].b;
		}
		return self->topFrame->codes + vmc->b;
	}else if( vmc[1].c == DAO_CASE_UNORDERED ){
		for(id=1; id<=vmc->c; id++){
			mid = vmc + id;
			if( DaoValue_Compare( opa, cst[ mid->a ] ) ==0 ){
				return self->topFrame->codes + mid->b;
			}
		}
	}
	first = 1;
	last = vmc->c;
	while( first <= last ){
		id = ( first + last ) / 2;
		mid = vmc + id;
		cmp = DaoValue_Compare( opa, cst[ mid->a ] );
		if( cmp ==0 ){
			if( cst[mid->a]->type== DAO_TUPLE && cst[mid->a]->xTuple.subtype == DAO_PAIR ){
				while( id > first && DaoValue_Compare( opa, cst[ vmc[id-1].a ] ) ==0 ) id --;
				mid = vmc + id;
			}
			return self->topFrame->codes + mid->b;
		}else if( cmp <0 ){
			last = id - 1;
		}else{
			first = id + 1;
		}
	}
	return self->topFrame->codes + vmc->b;
}
int DaoProcess_Move( DaoProcess *self, DaoValue *A, DaoValue **C, DaoType *t )
{
	if( ! DaoValue_Move( A, C, t ) ){
		DaoType *type;
		if( self->activeCode->code == DVM_MOVE || self->activeCode->code == DVM_MOVE_PP ){
			if( (A->type == DAO_CDATA || A->type == DAO_CSTRUCT) && t && t->tid == A->type ){
				if( DaoType_MatchTo( A->xCdata.ctype, t, NULL ) ){
					DaoValue_Copy( A, C );
					return 1;
				}
			}
		}
		type = DaoNamespace_GetType( self->activeNamespace, A );
		DaoProcess_RaiseTypeError( self, type, t, "moving" );
		return 0;
	}
	return 1;
}

DaoValue* DaoProcess_SetValue( DaoProcess *self, ushort_t reg, DaoValue *value )
{
	DaoType *tp = self->activeTypes[reg];
	int res = DaoValue_MoveX( value, self->activeValues + reg, tp, self->cache );
	if( res ) return self->activeValues[ reg ];
	return NULL;
}
DaoValue* DaoProcess_PutValue( DaoProcess *self, DaoValue *value )
{
	return DaoProcess_SetValue( self, self->activeCode->c, value );
}
int DaoProcess_PutReference( DaoProcess *self, DaoValue *refer )
{
	int tm, reg = self->activeCode->c;
	DaoValue **value = & self->activeValues[reg];
	DaoType *tp2, *tp = self->activeTypes[reg];

	if( *value == refer ) return 1;
	if( !(refer->xBase.trait & DAO_VALUE_CONST) ){
		if( tp == NULL ){
			GC_ShiftRC( refer, *value );
			*value = refer;
			return 1;
		}
		tm = DaoType_MatchValue( tp, refer, NULL );
		if( tm == DAO_MT_EQ ){
			GC_ShiftRC( refer, *value );
			*value = refer;
			return 1;
		}
	}
	if( DaoValue_MoveX( refer, value, tp, self->cache ) == 0 ) goto TypeNotMatching;
	return 0;
TypeNotMatching:
	tp2 = DaoNamespace_GetType( self->activeNamespace, refer );
	DaoProcess_RaiseTypeError( self, tp2, tp, "referencing" );
	return 0;
}
DaoNone* DaoProcess_PutNone( DaoProcess *self )
{
	DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) dao_none_value );
	return (DaoNone*) dao_none_value;
}
daoint* DaoProcess_PutInteger( DaoProcess *self, daoint value )
{
	DaoInteger tmp = {DAO_INTEGER,0,0,0,0,0};
	DaoValue *res = DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) & tmp );
	if( res ==NULL ) return NULL;
	res->xInteger.value = value;
	return & res->xInteger.value;
}
float* DaoProcess_PutFloat( DaoProcess *self, float value )
{
	DaoFloat tmp = {DAO_FLOAT,0,0,0,0,0.0};
	DaoValue *res = DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) & tmp );
	if( res ==NULL ) return NULL;
	res->xFloat.value = value;
	return & res->xFloat.value;
}
double* DaoProcess_PutDouble( DaoProcess *self, double value )
{
	DaoDouble tmp = {DAO_DOUBLE,0,0,0,0,0.0};
	DaoValue *res = DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) & tmp );
	if( res ==NULL ) return NULL;
	res->xDouble.value = value;
	return & res->xDouble.value;
}
complex16* DaoProcess_PutComplex( DaoProcess *self, complex16 value )
{
	DaoComplex tmp = {DAO_COMPLEX,0,0,0,0,{0.0,0.0}};
	DaoValue *res;
	tmp.value = value;
	res = DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) & tmp );
	if( res ==NULL ) return NULL;
	return & res->xComplex.value;
}
DString* DaoProcess_PutMBString( DaoProcess *self, const char *mbs )
{
	DString str = DString_WrapMBS( mbs );
	DaoString tmp = {DAO_STRING,0,0,0,0,NULL};
	DaoValue *res, *dest;
	tmp.data = & str;
	dest = self->activeValues[ self->activeCode->c ];
	if( dest && dest->type == DAO_STRING ){
		DString_Reset( dest->xString.data, 0 );
		DString_ToMBS( dest->xString.data );
	}
	res = DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) & tmp );
	if( res ==NULL ) return NULL;
	DString_ToMBS( res->xString.data );
	return res->xString.data;
}
DString* DaoProcess_PutWCString( DaoProcess *self, const wchar_t *wcs )
{
	DString str = DString_WrapWCS( wcs );
	DaoString tmp = {DAO_STRING,0,0,0,0,NULL};
	DaoValue *res, *dest;
	tmp.data = & str;
	dest = self->activeValues[ self->activeCode->c ];
	if( dest && dest->type == DAO_STRING ){
		DString_Reset( dest->xString.data, 0 );
		DString_ToWCS( dest->xString.data );
	}
	res = DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) & tmp );
	if( res ==NULL ) return NULL;
	DString_ToWCS( res->xString.data );
	return res->xString.data;
}
DString* DaoProcess_PutString( DaoProcess *self, DString *str )
{
	DaoString tmp = {DAO_STRING,0,0,0,0,NULL};
	DaoValue *res;
	tmp.data = str;
	res = DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) & tmp );
	if( res ==NULL ) return NULL;
	return res->xString.data;
}
DString* DaoProcess_PutBytes( DaoProcess *self, const char *bytes, daoint N )
{
	DString str = DString_WrapBytes( bytes, N );
	DaoString tmp = {DAO_STRING,0,0,0,0,NULL};
	DaoValue *res;
	tmp.data = & str;
	res = DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) & tmp );
	if( res ==NULL ) return NULL;
	return res->xString.data;
}
#ifdef DAO_WITH_NUMARRAY
DaoArray* DaoProcess_PutVectorSB( DaoProcess *self, signed  char *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_INTEGER );
	if( array ) DaoArray_SetVectorSB( res, array, N );
	return res;
}
DaoArray* DaoProcess_PutVectorUB( DaoProcess *self, unsigned char *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_INTEGER );
	if( array ) DaoArray_SetVectorUB( res, array, N );
	return res;
}
DaoArray* DaoProcess_PutVectorSS( DaoProcess *self, signed  short *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_INTEGER );
	if( array ) DaoArray_SetVectorSS( res, array, N );
	return res;
}
DaoArray* DaoProcess_PutVectorUS( DaoProcess *self, unsigned short *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_INTEGER );
	if( array ) DaoArray_SetVectorUS( res, array, N );
	return res;
}
DaoArray* DaoProcess_PutVectorSI( DaoProcess *self, signed  int *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_INTEGER );
	if( array ) DaoArray_SetVectorSI( res, array, N );
	return res;
}
DaoArray* DaoProcess_PutVectorUI( DaoProcess *self, unsigned int *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_INTEGER );
	if( array ) DaoArray_SetVectorUI( res, array, N );
	return res;
}
DaoArray* DaoProcess_PutVectorI( DaoProcess *self, daoint *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_INTEGER );
	if( array ) DaoArray_SetVectorI( res, array, N );
	return res;
}
DaoArray* DaoProcess_PutVectorF( DaoProcess *self, float *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_FLOAT );
	if( array ) DaoArray_SetVectorF( res, array, N );
	return res;
}
DaoArray* DaoProcess_PutVectorD( DaoProcess *self, double *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_DOUBLE );
	if( array ) DaoArray_SetVectorD( res, array, N );
	return res;
}
DaoArray* DaoProcess_PutVectorC( DaoProcess *self, complex16 *array, daoint N )
{
	DaoArray *res = DaoProcess_GetArray( self, self->activeCode );
	DaoArray_SetNumType( res, DAO_COMPLEX );
	if( array ) DaoArray_SetVectorD( res, (double*)array, N );
	return res;
}
#else
static DaoArray* NullArray( DaoProcess *self )
{
	DaoProcess_RaiseException( self, DAO_ERROR, getCtInfo( DAO_DISABLED_NUMARRAY ) );
	return NULL;
}
DaoArray* DaoProcess_PutVectorSB( DaoProcess *s, signed  char *v, daoint N ){ return NullArray(s); }
DaoArray* DaoProcess_PutVectorUB( DaoProcess *s, unsigned char *v, daoint N ){ return NullArray(s); }
DaoArray* DaoProcess_PutVectorSS( DaoProcess *s, signed  short *v, daoint N ){ return NullArray(s); }
DaoArray* DaoProcess_PutVectorUS( DaoProcess *s, unsigned short *v, daoint N ){ return NullArray(s); }
DaoArray* DaoProcess_PutVectorSI( DaoProcess *s, signed  int *v, daoint N ){ return NullArray(s); }
DaoArray* DaoProcess_PutVectorUI( DaoProcess *s, unsigned int *v, daoint N ){ return NullArray(s); }
DaoArray* DaoProcess_PutVectorI( DaoProcess *s, daoint *v, daoint n ){ return NullArray(s); }
DaoArray* DaoProcess_PutVectorF( DaoProcess *s, float *v, daoint n ){ return NullArray(s); }
DaoArray* DaoProcess_PutVectorD( DaoProcess *s, double *v, daoint n ){ return NullArray(s); }
DaoArray* DaoProcess_PutVectorC( DaoProcess *s, complex16 *v, daoint n ){ return NullArray(s); }
#endif
DaoList* DaoProcess_PutList( DaoProcess *self )
{
	return DaoProcess_GetList( self, self->activeCode );
}
DaoMap* DaoProcess_PutMap( DaoProcess *self, unsigned int hashing )
{
	return DaoProcess_GetMap( self, self->activeCode, hashing );
}
DaoArray* DaoProcess_PutArray( DaoProcess *self )
{
	return DaoProcess_GetArray( self, self->activeCode );
}
DaoStream* DaoProcess_PutFile( DaoProcess *self, FILE *file )
{
	DaoStream *stream = DaoStream_New();
	DaoStream_SetFile( stream, file );
	if( DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*) stream ) ) return stream;
	DaoStream_Delete( stream );
	return NULL;
}
void DaoCdata_Delete( DaoCdata *self );
DaoCdata* DaoProcess_PutCdata( DaoProcess *self, void *data, DaoType *type )
{
	DaoCdata *cdata = DaoCdata_New( type, data );
	if( DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*)cdata ) ) return cdata;
	DaoGC_TryDelete( (DaoValue*) cdata );
	return NULL;
}
DaoCdata* DaoProcess_WrapCdata( DaoProcess *self, void *data, DaoType *type )
{
	DaoCdata *cdata = DaoCdata_Wrap( type, data );
	if( DaoProcess_SetValue( self, self->activeCode->c, (DaoValue*)cdata ) ) return cdata;
	DaoGC_TryDelete( (DaoValue*) cdata );
	return NULL;
}
DaoCdata*  DaoProcess_CopyCdata( DaoProcess *self, void *d, int n, DaoType *t )
{
	DaoCdata *cdt;
	void *d2 = dao_malloc( n );
	memcpy( d2, d, n );
	cdt = DaoProcess_PutCdata( self, d2, t );
	return cdt;
}
DaoType* DaoProcess_GetCallReturnType( DaoProcess *self, DaoVmCode *vmc, int tid )
{
	DaoType *type = self->activeTypes[ vmc->c ];

	if( type == NULL ) return NULL;
	if( type->tid == DAO_VARIANT ) type = DaoType_GetVariantItem( type, tid );
	if( type == NULL || !(type->tid & DAO_ANY) ) return type;

	if( vmc->code == DVM_CALL || vmc->code == DVM_MCALL ){
		DaoRoutine *rout = (DaoRoutine*) self->activeValues[ vmc->a ];
		if( rout && rout->type == DAO_ROUTINE ) type = (DaoType*) rout->routType->aux;
	}
	return type;
}
DLong* DaoProcess_GetLong( DaoProcess *self, DaoVmCode *vmc )
{
#ifdef DAO_WITH_LONGINT
	DaoType *tp = DaoProcess_GetCallReturnType( self, vmc, DAO_LONG );
	DaoValue *dC = self->activeValues[ vmc->c ];
	if( dC && dC->type == DAO_LONG ){
		dC->xLong.value->sign = 1;
		dC->xLong.value->base = 10;
		dC->xLong.value->size = 0;
		return dC->xLong.value;
	}
	if( tp && tp->tid != DAO_LONG && !(tp->tid & DAO_ANY) ) return NULL;
	dC = (DaoValue*) DaoLong_New();
	GC_ShiftRC( dC, self->activeValues[ vmc->c ] );
	self->activeValues[ vmc->c ] = dC;
	return dC->xLong.value;
#else
	self->activeCode = vmc;
	DaoProcess_RaiseException( self, DAO_ERROR, getCtInfo( DAO_DISABLED_LONGINT ) );
	return NULL;
#endif
}
DLong* DaoProcess_PutLong( DaoProcess *self )
{
	return DaoProcess_GetLong( self, self->activeCode );
}
DaoEnum* DaoProcess_GetEnum( DaoProcess *self, DaoVmCode *vmc )
{
	DaoType *tp = DaoProcess_GetCallReturnType( self, vmc, DAO_ENUM );
	DaoValue *dC = self->activeValues[ vmc->c ];

	if( tp && (tp->tid & DAO_ANY) ) tp = NULL;
	if( tp && tp->tid != DAO_ENUM ) return NULL;
	if( dC && dC->type == DAO_ENUM && tp->tid == DAO_ENUM ){
		if( tp != dC->xEnum.etype ) DaoEnum_SetType( & dC->xEnum, tp );
		return & dC->xEnum;
	}
	dC = (DaoValue*) DaoEnum_New( tp, 0 );
	GC_ShiftRC( dC, self->activeValues[ vmc->c ] );
	self->activeValues[ vmc->c ] = dC;
	return & dC->xEnum;
}
DaoEnum* DaoProcess_PutEnum( DaoProcess *self, const char *symbols )
{
	DaoEnum *denum = DaoProcess_GetEnum( self, self->activeCode );
	DaoEnum_SetSymbols( denum, symbols );
	return denum;
}
/**/
DaoList* DaoProcess_GetListByType( DaoProcess *self, DaoVmCode *vmc, DaoType *tp )
{
	/* create a new list in any case. */
	DaoList *list = (DaoList*)self->activeValues[ vmc->c ];
	if( list && list->type == DAO_LIST && list->unitype == tp ){
		if( list->refCount == 1 ){
			DaoList_Clear( list );
			return list;
		}
		if( list->refCount == 2 && !(self->trait & DAO_VALUE_CONST) ){
			DaoVmCode *vmc2 = vmc + 1;
			if( (vmc2->code == DVM_MOVE || vmc2->code == DVM_MOVE_PP) && vmc2->a != vmc2->c ){
				if( self->activeValues[vmc2->c] == (DaoValue*) list ){
					DaoList_Clear( list );
					return list;
				}
			}
		}
	}
	if( tp == NULL || tp->tid != DAO_LIST ) tp = dao_list_any;
	list = DaoDataCache_MakeList( self->cache, tp );
	DaoValue_Move( (DaoValue*) list, self->activeValues + vmc->c, tp );
	return list;
}
DaoList* DaoProcess_GetList( DaoProcess *self, DaoVmCode *vmc )
{
	DaoType *tp = DaoProcess_GetCallReturnType( self, vmc, DAO_LIST );
	return DaoProcess_GetListByType( self, vmc, tp );
}
DaoMap* DaoProcess_GetMap( DaoProcess *self,  DaoVmCode *vmc, unsigned int hashing )
{
	DaoMap *map = (DaoMap*) self->activeValues[ vmc->c ];
	DaoType *tp = DaoProcess_GetCallReturnType( self, vmc, DAO_MAP );

	if( map && map->type == DAO_MAP && map->unitype == tp ){
		if( (map->items->hashing == 0) == (hashing == 0) ){
			if( map->refCount == 1 ){
				DaoMap_Reset( map );
				map->items->hashing = hashing;
				return map;
			}
			if( map->refCount == 2 && !(self->trait & DAO_VALUE_CONST) ){
				DaoVmCode *vmc2 = vmc + 1;
				if( (vmc2->code == DVM_MOVE || vmc2->code == DVM_MOVE_PP) && vmc2->a != vmc2->c ){
					if( self->activeValues[vmc2->c] == (DaoValue*) map ){
						DaoMap_Reset( map );
						map->items->hashing = hashing;
						return map;
					}
				}
			}
		}
	}
	if( tp == NULL || tp->tid != DAO_MAP ) tp = dao_map_any;
	map = DaoDataCache_MakeMap( self->cache, tp, hashing );
	DaoValue_Move( (DaoValue*) map, self->activeValues + vmc->c, tp );
	return map;
}

DaoArray* DaoProcess_GetArrayByType( DaoProcess *self, DaoVmCode *vmc, DaoType *tp )
{
#ifdef DAO_WITH_NUMARRAY
	DaoValue *dC = self->activeValues[ vmc->c ];
	DaoArray *array = (DaoArray*) dC;
	int type = DAO_NONE;
	if( tp && tp->tid == DAO_ARRAY && tp->nested->size ){
		type = tp->nested->items.pType[0]->tid;
		if( type > DAO_COMPLEX ) type = DAO_NONE;
	}
	if( type && array && array->type == DAO_ARRAY && array->etype == type ){
		if( array->refCount == 1 ) return array;
		if( array->refCount == 2 && !(self->trait & DAO_VALUE_CONST) ){
			DaoVmCode *vmc2 = vmc + 1;
			if( (vmc2->code == DVM_MOVE || vmc2->code == DVM_MOVE_PP) && vmc2->a != vmc2->c ){
				if( self->activeValues[vmc2->c] == (DaoValue*) array ){
					return array;
				}
			}
		}
	}
	if( dC && dC->type == DAO_ARRAY && dC->xArray.refCount == 1 ){
		GC_DecRC( dC->xArray.original );
		dC->xArray.original = NULL;
		DaoArray_SetNumType( (DaoArray*) dC, type );
	}else{
		dC = (DaoValue*) DaoDataCache_MakeArray( self->cache, type );
		DaoValue_Copy( dC, & self->activeValues[ vmc->c ] );
	}
	return & dC->xArray;
#else
	self->activeCode = vmc;
	DaoProcess_RaiseException( self, DAO_ERROR, getCtInfo( DAO_DISABLED_NUMARRAY ) );
	return NULL;
#endif
}
DaoArray* DaoProcess_GetArray( DaoProcess *self, DaoVmCode *vmc )
{
	DaoType *tp = DaoProcess_GetCallReturnType( self, vmc, DAO_ARRAY );
	return DaoProcess_GetArrayByType( self, vmc, tp );
}
DaoTuple* DaoProcess_GetTuple( DaoProcess *self, DaoType *type, int size, int init )
{
	DaoValue *val = self->activeValues[ self->activeCode->c ];
	DaoTuple *tup = val && val->type == DAO_TUPLE ? & val->xTuple : NULL;

	if( tup && tup->unitype == type && tup->size == size ){
		if( tup->refCount == 1 ) return tup;
		if( tup->refCount == 2 && !(self->trait & DAO_VALUE_CONST) ){
			DaoVmCode *vmc = self->activeCode + 1;
			int code = vmc->code;
			if( (code == DVM_MOVE || code == DVM_MOVE_PP) && vmc->a != vmc->c ){
				if( self->activeValues[vmc->c] == (DaoValue*) tup ) return tup;
			}
		}
	}
	tup = DaoDataCache_MakeTuple( self->cache, type, size, init );
	GC_ShiftRC( tup, val );
	self->activeValues[ self->activeCode->c ] = (DaoValue*) tup;
	return tup;
}
DaoTuple* DaoProcess_PutTuple( DaoProcess *self, int size )
{
	int i, N = abs(size);
	int M = self->factory->size;
	DaoValue **values = self->factory->items.pValue;
	DaoType *type = DaoProcess_GetCallReturnType( self, self->activeCode, DAO_TUPLE );
	DaoTuple *tuple;

	if( type == NULL || type->tid != DAO_TUPLE ) return NULL;
	if( size == 0 ) return DaoProcess_GetTuple( self, type, type->nested->size, 1 );
	if( type->variadic == 0 && N != type->nested->size ) return NULL;
	if( N < type->nested->size ) return NULL;
	tuple = DaoProcess_GetTuple( self, type, N, size > 0 );
	if( size > 0 ) return tuple;
	if( M < size ) return NULL;
	for(i=0; i<N; i++) DaoTuple_SetItem( tuple, values[M-N+i], i );
	DArray_Erase( self->factory, M - size, -1 );
	return tuple;
}
DaoType* DaoProcess_GetReturnType( DaoProcess *self )
{
	DaoStackFrame *frame = self->topFrame;
	DaoType *type = self->activeTypes[ self->activeCode->c ]; /* could be specialized; */
	if( frame->retype ) return frame->retype;
	if( type == NULL || (type->attrib & DAO_TYPE_UNDEF) ){
		if( frame->routine ) type = (DaoType*) frame->routine->routType->aux;
	}
	if( type == NULL ) type = self->activeTypes[ self->activeCode->c ];
	GC_ShiftRC( type, frame->retype );
	frame->retype = type;
	return type;
}

void DaoProcess_MakeTuple( DaoProcess *self, DaoTuple *tuple, DaoValue *its[], int N )
{
	DaoType **types, *tp, *vlt = NULL, *ct = tuple->unitype;
	int i, M;
	if( ct == NULL ) return;
	if( ct->nested == NULL || (ct->nested->size - (ct->variadic != 0)) > N ){
		DaoProcess_RaiseException( self, DAO_ERROR, "invalid tuple enumeration" );
		return;
	}
	types = ct->nested->items.pType;
	M = ct->nested->size - (ct->variadic != 0);
	if( ct->variadic ) vlt = (DaoType*) types[M]->aux;
	for(i=0; i<N; i++){
		DaoValue *val = its[i];
		if( val->type == DAO_PAR_NAMED ){
			DaoNameValue *nameva = & val->xNameValue;
			DNode *node = MAP_Find( ct->mapNames, nameva->name );
			if( node == NULL || node->value.pInt != i ){
				DaoProcess_RaiseException( self, DAO_ERROR, "name not matched" );
				return;
			}
			val = nameva->value;
		}
		tp = i < M ? types[i] : vlt;
		if( tp && tp->tid == DAO_PAR_NAMED ) tp = & tp->aux->xType;
		if( DaoValue_MoveX( val, tuple->items + i, tp, self->cache ) == 0 ){
			DaoProcess_RaiseException( self, DAO_ERROR, "invalid tuple enumeration" );
			return;
		}
	}
}

void DaoProcess_BindNameValue( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *dB = self->activeValues[ vmc->b ];
	DaoValue *dC = self->activeValues[ vmc->c ];
	DaoType *type = self->activeTypes[ vmc->c ];
	DaoNameValue *nameva = NULL;
	if( type && dC && dC->type == DAO_PAR_NAMED && dC->xNameValue.unitype == type ){
		DaoNameValue *NV = (DaoNameValue*) dC;
		DaoVmCode *vmc2 = vmc + 1;
		uchar_t codetype = DaoVmCode_GetOpcodeType( vmc2 );
		if( NV->refCount == 1 ){
			nameva = NV;
		}else if( NV->refCount == 2 && codetype == DAO_CODE_MOVE && vmc2->a != vmc2->c ){
			if( self->activeValues[vmc2->c] == dC ) nameva = NV;
		}
	}
	if( nameva == NULL ){
		DaoString *S = (DaoString*) self->activeRoutine->routConsts->items.items.pValue[ vmc->a ];
		if( type == NULL ){
			DaoNamespace *ns = self->activeNamespace;
			DaoValue *tp = (DaoValue*) DaoNamespace_GetType( ns, dB );
			type = DaoNamespace_MakeType( ns, S->data->mbs, DAO_PAR_NAMED, tp, NULL, 0 );
		}
		nameva = DaoNameValue_New( S->data, NULL );
		nameva->unitype = type;
		GC_IncRC( nameva->unitype );
		DaoProcess_SetValue( self, vmc->c, (DaoValue*) nameva );
	}
	DaoValue_Move( dB, & nameva->value, (DaoType*) nameva->unitype->aux );
}
void DaoProcess_DoPair( DaoProcess *self, DaoVmCode *vmc )
{
	DaoNamespace *ns = self->activeNamespace;
	DaoType *tp = self->activeTypes[ vmc->c ];
	DaoType *ta = self->activeTypes[ vmc->a ];
	DaoType *tb = self->activeTypes[ vmc->b ];
	DaoTuple *tuple;
	self->activeCode = vmc;
	//XXX if( tp == NULL ) tp = DaoNamespace_MakePairValueType( ns, dA, dB );
	if( ta == NULL ) ta = DaoNamespace_GetType( ns, self->activeValues[ vmc->a ] );
	if( tb == NULL ) tb = DaoNamespace_GetType( ns, self->activeValues[ vmc->b ] );
	if( tp == NULL ) tp = DaoNamespace_MakePairType( ns, ta, tb );
	tuple = DaoProcess_GetTuple( self, tp, 2, 1 );
	tuple->subtype = DAO_PAIR;
	DaoValue_Copy( self->activeValues[ vmc->a ], & tuple->items[0] );
	DaoValue_Copy( self->activeValues[ vmc->b ], & tuple->items[1] );
}
void DaoProcess_DoTuple( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *val;
	DaoTuple *tuple;
	DaoType *tp, *ct = self->activeTypes[ vmc->c ];
	int argstuple = vmc->a == 0 && vmc->b == self->activeRoutine->parCount;
	int i, count = argstuple ? self->topFrame->parCount : vmc->b;

	self->activeCode = vmc;
	tuple = DaoProcess_GetTuple( self, ct && ct->variadic == 0 ? ct : NULL, count, 0 );
	if( ct == NULL ){
		DaoNamespace *ns = self->activeNamespace;
		ct = DaoType_New( "tuple<", DAO_TUPLE, NULL, NULL );
		for(i=0; i<count; i++){
			val = self->activeValues[ vmc->a + i ];
			tp = DaoNamespace_GetType( ns, val );
			if( tp == NULL ) tp = DaoNamespace_GetType( ns, dao_none_value );
			if( i >0 ) DString_AppendMBS( ct->name, "," );
			if( tp->tid == DAO_PAR_NAMED ){
				DaoNameValue *nameva = & val->xNameValue;
				if( ct->mapNames == NULL ) ct->mapNames = DMap_New(D_STRING,0);
				MAP_Insert( ct->mapNames, nameva->name, i );
				DString_Append( ct->name, nameva->name );
				DString_AppendMBS( ct->name, ":" );
				DString_Append( ct->name, tp->aux->xType.name );
				val = nameva->value;
			}else{
				DString_Append( ct->name, tp->name );
			}
			DArray_Append( ct->nested, tp );
			DaoTuple_SetItem( tuple, val, i );
		}
		DString_AppendMBS( ct->name, ">" );
		tp = DaoNamespace_FindType( ns, ct->name );
		if( tp ){
			DaoType_Delete( ct );
			ct = tp;
		}else{
			DaoType_CheckAttributes( ct );
			DaoType_InitDefault( ct );
			DaoNamespace_AddType( ns, ct->name, ct );
		}
		tuple->unitype = ct;
		GC_IncRC( ct );
	}else if( argstuple ){
		GC_ShiftRC( ct, tuple->unitype );
		tuple->unitype = ct;
		for(i=0; i<count; i++) DaoTuple_SetItem( tuple, self->activeValues[vmc->a + i], i );
	}else{
		if( tuple->unitype == NULL ){
			tuple->unitype = ct;
			GC_IncRC( ct );
		}
		DaoProcess_MakeTuple( self, tuple, self->activeValues + vmc->a, count );
	}
}
void DaoProcess_DoCheck( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *dA = self->activeValues[ vmc->a ];
	DaoValue *dB = self->activeValues[ vmc->b ];
	DaoType *type = (DaoType*) dB;
	daoint *res = 0;
	self->activeCode = vmc;
	res = DaoProcess_PutInteger( self, 0 );
	if( dA->type && dB->type == DAO_TYPE ){
		if( dA->type == DAO_OBJECT ) dA = (DaoValue*) dA->xObject.rootObject;
		if( type->tid == DAO_VARIANT ){
			int i, n, mt = 0, id = 0, max = 0;
			for(i=0,n=type->nested->size; i<n; i++){
				if( dA->type == DAO_TYPE ){
					mt = DaoType_MatchTo( & dA->xType, type->nested->items.pType[i], NULL );
				}else{
					mt = DaoType_MatchValue( type->nested->items.pType[i], dA, NULL );
				}
				if( mt > max ){
					max = mt;
					id = i + 1;
				}
				if( max == DAO_MT_EQ ) break;
			}
			*res = id;
			return;
		}
		if( dA->type < DAO_ARRAY ){
			*res = dA->type == type->tid;
		}else{
			*res = DaoType_MatchValue( type, dA, NULL ) != 0;
		}
	}else if( dA->type == dB->type ){
		*res = 1;
		if( dA->type == DAO_OBJECT ){
			*res = dA->xObject.rootObject->defClass == dB->xObject.rootObject->defClass;
		}else if( dA->type == DAO_CDATA || dA->type == DAO_CSTRUCT ){
			*res = dA->xCdata.ctype == dB->xCdata.ctype;
		}else if( dA->type >= DAO_ARRAY && dA->type <= DAO_TUPLE ){
			DaoType *t1 = NULL;
			DaoType *t2 = NULL;
			*res = 0;
			switch( dA->type ){
				case DAO_ARRAY :
					t1 = dao_array_types[ dA->xArray.etype ];
					t2 = dao_array_types[ dB->xArray.etype ];
					break;
				case DAO_LIST : t1 = dA->xList.unitype; t2 = dB->xList.unitype; break;
				case DAO_MAP  : t1 = dA->xMap.unitype;  t2 = dB->xMap.unitype; break;
				case DAO_TUPLE : t1 = dA->xTuple.unitype; t2 = dB->xTuple.unitype; break;
				default : break;
			}
			*res = DaoType_MatchTo( t1, t2, NULL ) == DAO_MT_EQ;
		}
	}
}
void DaoProcess_DoGetItem( DaoProcess *self, DaoVmCode *vmc )
{
	daoint id;
	DaoValue *B = dao_none_value;
	DaoValue *A = self->activeValues[ vmc->a ];
	DaoType *ct = self->activeTypes[ vmc->c ];
	DaoTypeCore *tc = DaoValue_GetTyper( A )->core;

	self->activeCode = vmc;
	if( A == NULL || A->type == 0 ){
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "on none object" );
		return;
	}
	if( vmc->code == DVM_GETI ) B = self->activeValues[ vmc->b ];
	if( A->type == DAO_LIST && (B->type >= DAO_INTEGER && B->type <= DAO_DOUBLE ) ){
		DaoList *list = & A->xList;
		id = DaoValue_GetInteger( B );
		if( id < 0 ) id += list->items.size;
		if( id >=0 && id < list->items.size ){
			GC_ShiftRC( list->items.items.pValue[id], self->activeValues[ vmc->c ] );
			self->activeValues[ vmc->c ] = list->items.items.pValue[id];
		}else{
			DaoProcess_RaiseException( self, DAO_ERROR, "index out of range" );
			return;
		}
#ifdef DAO_WITH_NUMARRAY
	}else if( A->type == DAO_ARRAY && (B->type >=DAO_INTEGER && B->type <=DAO_DOUBLE )){
		DaoValue temp = {0};
		DaoValue *C = (DaoValue*) & temp;
		DaoArray *na = & A->xArray;
		id = DaoValue_GetInteger( B );
		memset( C, 0, sizeof(DaoValue) );
		if( na->original && DaoArray_Sliced( na ) == 0 ){
			DaoProcess_RaiseException( self, DAO_ERROR_INDEX, "slicing" );
			return;
		}
		if( id < 0 ) id += na->size;
		if( id < 0 || id >= na->size ){
			DaoProcess_RaiseException( self, DAO_ERROR_INDEX_OUTOFRANGE, "" );
			return;
		}
		C->type = na->etype;
		switch( na->etype ){
			case DAO_INTEGER : C->xInteger.value = na->data.i[id]; break;
			case DAO_FLOAT   : C->xFloat.value = na->data.f[id];  break;
			case DAO_DOUBLE  : C->xDouble.value = na->data.d[id];  break;
			case DAO_COMPLEX : C->xComplex.value = na->data.c[id]; break;
			default : break;
		}
		DaoProcess_Move( self, C, & self->activeValues[ vmc->c ], ct );
#endif
	}else if( vmc->code == DVM_GETI ){
		tc->GetItem( A, self, self->activeValues + vmc->b, 1 );
	}else if( vmc->code == DVM_GETDI ){
		DaoInteger iv = {DAO_INTEGER,0,0,0,1,0};
		DaoValue *piv = (DaoValue*) (DaoInteger*) & iv;
		iv.value = vmc->b;
		tc->GetItem( A, self, & piv, 1 );
	}else if( vmc->code == DVM_GETMI || (vmc->code >= DVM_GETMI_AII && vmc->code <= DVM_GETMI_ACI) ){
		tc->GetItem( A, self, self->activeValues + vmc->a + 1, vmc->b );
	}
}
void DaoProcess_DoGetField( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *C, *A = self->activeValues[ vmc->a ];
	DaoTypeCore *tc = DaoValue_GetTyper( A )->core;
	DaoNamespace *ns = self->activeNamespace;
	DString *name = self->activeRoutine->routConsts->items.items.pValue[ vmc->b ]->xString.data;
	DArray *elist = self->exceptions;
	daoint E = elist->size;

	self->activeCode = vmc;
	if( A == NULL || A->type == 0 ){
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "on none object" );
		return;
	}
	tc->GetField( A, self, name );
	if( elist->size != (E + 1) ) return;
	if( elist->items.pCdata[E]->ctype != DaoException_GetType( DAO_ERROR_FIELD_NOTEXIST ) ) return;
	C = DaoValue_FindAuxMethod( A, name, ns );
	if( C == NULL ) return;
	DArray_PopBack( elist );
	DaoProcess_PutValue( self, C );
}



void DaoProcess_DoSetItem( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *A, *B = dao_none_value, *C = self->activeValues[ vmc->c ];
	DaoTypeCore *tc = DaoValue_GetTyper( C )->core;
	daoint id, rc = 0;

	self->activeCode = vmc;
	A = self->activeValues[ vmc->a ];
	if( C == NULL || C->type == 0 ){
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "on none object" );
		return;
	}

	if( vmc->code == DVM_SETI ) B = self->activeValues[ vmc->b ];
	if( C->type == DAO_LIST && B->type == DAO_INTEGER ){
		rc = DaoList_SetItem( & C->xList, A, B->xInteger.value );
	}else if( C->type == DAO_LIST && B->type == DAO_FLOAT ){
		rc = DaoList_SetItem( & C->xList, A, (int) B->xFloat.value );
	}else if( C->type == DAO_LIST && B->type == DAO_DOUBLE ){
		rc = DaoList_SetItem( & C->xList, A, (int) B->xDouble.value );
#ifdef DAO_WITH_NUMARRAY
	}else if( C->type == DAO_ARRAY && (B->type >=DAO_INTEGER && B->type <=DAO_DOUBLE)
			 && (A->type >=DAO_INTEGER && A->type <=DAO_DOUBLE) ){
		DaoArray *na = & C->xArray;
		double val = DaoValue_GetDouble( A );
		complex16 cpx = DaoValue_GetComplex( A );
		id = DaoValue_GetDouble( B );
		if( na->original && DaoArray_Sliced( na ) == 0 ){
			DaoProcess_RaiseException( self, DAO_ERROR_INDEX, "slicing" );
			return;
		}
		if( id < 0 ) id += na->size;
		if( id < 0 || id >= na->size ){
			DaoProcess_RaiseException( self, DAO_ERROR_INDEX_OUTOFRANGE, "" );
			return;
		}
		switch( na->etype ){
			case DAO_INTEGER : na->data.i[ id ] = (daoint) val; break;
			case DAO_FLOAT  : na->data.f[ id ] = (float) val; break;
			case DAO_DOUBLE : na->data.d[ id ] = val; break;
			case DAO_COMPLEX : na->data.c[ id ] = cpx; break;
			default : break;
		}
#endif
	}else if( vmc->code == DVM_SETI ){
		tc->SetItem( C, self, self->activeValues + vmc->b, 1, A );
	}else if( vmc->code == DVM_GETDI ){
		DaoInteger iv = {DAO_INTEGER,0,0,0,1,0};
		DaoValue *piv = (DaoValue*) (DaoInteger*) & iv;
		iv.value = vmc->b;
		tc->SetItem( C, self, & piv, 1, A );
	}else if( vmc->code == DVM_SETMI || (vmc->code >= DVM_SETMI_AIII && vmc->code <= DVM_SETMI_ACIC) ){
		tc->SetItem( C, self, self->activeValues + vmc->c + 1, vmc->b, A );
	}
	if( rc ) DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "value type" );
}
void DaoProcess_DoSetField( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *A, *C = self->activeValues[ vmc->c ];
	DaoValue *fname = self->activeRoutine->routConsts->items.items.pValue[ vmc->b ];
	DaoTypeCore *tc = DaoValue_GetTyper( C )->core;

	self->activeCode = vmc;
	A = self->activeValues[ vmc->a ];
	if( C == NULL || C->type == 0 ){
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "on none object" );
		return;
	}
	tc->SetField( C, self, fname->xString.data, A );
}


DaoValue* DaoProcess_DoReturn( DaoProcess *self, DaoVmCode *vmc )
{
	DaoStackFrame *topFrame = self->topFrame;
	DaoType *type = NULL;
	DaoValue **src = self->activeValues + vmc->a;
	DaoValue **dest = self->stackValues;
	DaoValue *retValue = NULL;
	daoint i, n, returning = topFrame->returning;

	self->activeCode = vmc;

	if( returning != (ushort_t)-1 ){
		DaoStackFrame *lastframe = topFrame->prev;
#ifdef DEBUG
		assert( lastframe && lastframe->routine );
#endif
		type = lastframe->routine->body->regType->items.pType[ returning ];
		dest = self->stackValues + lastframe->stackBase + returning;
	}
	if( topFrame->routine->attribs & DAO_ROUT_INITOR ){
		retValue = (DaoValue*)self->activeObject;
	}else if( vmc->b == 1 ){
		retValue = self->activeValues[ vmc->a ];
	}else if( vmc->b > 1 && dest != self->stackValues ){
		DaoTuple *tup = (DaoTuple*) *dest;
		DaoTuple *tuple = NULL;
		if( tup && tup->type == DAO_TUPLE && tup->unitype == type && tup->refCount == 1 ){
			if( tup->size > vmc->b ) goto InvalidReturn;
			tuple = tup;
		}else if( type && type->tid == DAO_TUPLE ){
			if( type->nested->size > vmc->b ) goto InvalidReturn;
			tuple = DaoDataCache_MakeTuple( self->cache, type, vmc->b, 0 );
		}else{
			tuple = DaoDataCache_MakeTuple( self->cache, NULL, vmc->b, 0 );
		}
		if( tuple->unitype ){
			DaoType **TS = tuple->unitype->nested->items.pType;
			for(i=0,n=tuple->size; i<n; i++){
				DaoType *tp = TS[i]->tid == DAO_PAR_NAMED ? (DaoType*)TS[i]->aux : TS[i];
				DaoValue_Move( src[i], tuple->items + i, tp );
			}
		}else{
			for(i=0,n=tuple->size; i<n; i++) DaoValue_Copy( src[i], tuple->items + i );
		}
		retValue = (DaoValue*) tuple;
	}else if( vmc->b > 1 ){
		DaoTuple *tuple = DaoDataCache_MakeTuple( self->cache, NULL, vmc->b, 0 );
		retValue = (DaoValue*) tuple;
		for(i=0; i<vmc->b; i++) DaoValue_CopyX( src[i], tuple->items + i, self->cache );
	}else{
		retValue = dao_none_value;
		type = NULL;
	}
	if( retValue == NULL ){
		int cmdline = self->vmSpace->evalCmdline;
		int opt1 = self->vmSpace->options & DAO_OPTION_INTERUN;
		int opt2 = self->activeNamespace->options & DAO_NS_AUTO_GLOBAL;
		int retnull = type == NULL || type->tid == DAO_NONE || type->tid == DAO_UDT;
		int retnull2 = type && type->tid == DAO_VALTYPE && type->value->type == DAO_NONE;
		if( retnull || retnull2 || cmdline || (opt1 && opt2) ) retValue = dao_none_value;
	}
	if( DaoValue_MoveX( retValue, dest, type, self->cache ) ==0 ) goto InvalidReturn;
	return retValue;
InvalidReturn:
#if 0
	fprintf( stderr, "retValue = %p %i %p %s\n", retValue, retValue->type, type, type->name->mbs );
#endif
	DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "invalid returned value" );
	return NULL;
}
int DaoVM_DoMath( DaoProcess *self, DaoVmCode *vmc, DaoValue *C, DaoValue *A )
{
	DaoValue temp = {0};
	DaoValue *value = (DaoValue*) & temp;
	DaoNamespace *ns = self->activeRoutine->nameSpace;
	DaoType *type = self->activeTypes[vmc->c];
	int func = vmc->a;
	self->activeCode = vmc;
	memset( value, 0, sizeof(DaoValue) );
	if( A->type == DAO_COMPLEX ){
		complex16 par = A->xComplex.value;
		complex16 cres = {0.0,0.0};
		double rres = 0.0;
		int isreal = 0;
		switch( func ){
		case DVM_MATH_ABS  : rres = abs_c( par ); isreal = 1; break;
		case DVM_MATH_ARG  : rres = arg_c( par ); isreal = 1; break;
		case DVM_MATH_NORM  : rres = norm_c( par ); isreal = 1; break;
		case DVM_MATH_IMAG  : rres = par.imag; isreal = 1; break;
		case DVM_MATH_REAL  : rres = par.real; isreal = 1; break;
		case DVM_MATH_CEIL : cres = ceil_c( par ); break;
		case DVM_MATH_COS  : cres = cos_c( par );  break;
		case DVM_MATH_COSH : cres = cosh_c( par ); break;
		case DVM_MATH_EXP  : cres = exp_c( par );  break;
		case DVM_MATH_FLOOR : cres = floor_c( par ); break;
		case DVM_MATH_LOG  : cres = log_c( par );  break;
		case DVM_MATH_SIN  : cres = sin_c( par );  break;
		case DVM_MATH_SINH : cres = sinh_c( par ); break;
		case DVM_MATH_SQRT : cres = sqrt_c( par ); break;
		case DVM_MATH_TAN  : cres = tan_c( par );  break;
		case DVM_MATH_TANH : cres = tanh_c( par ); break;
		default : return 1;
		}
		if( isreal ){
			if( C && C->type == DAO_DOUBLE ){
				C->xDouble.value = rres;
			}else{
				value->type = DAO_DOUBLE;
				value->xDouble.value = rres;
				return DaoValue_Move( value, self->activeValues + vmc->c, dao_type_double ) == 0;
			}
		}else{
			if( C && C->type == DAO_COMPLEX ){
				C->xComplex.value = cres;
			}else{
				value->type = DAO_COMPLEX;
				value->xComplex.value = cres;
				return DaoValue_Move( value, self->activeValues + vmc->c, dao_type_complex ) == 0;
			}
		}
		return 0;
	}else if( A->type == DAO_INTEGER && func <= DVM_MATH_ABS ){
		daoint res = A->xInteger.value;
		switch( func ){
		case DVM_MATH_RAND : res = res * DaoProcess_Random( self ); break;
		case DVM_MATH_ABS  : res = abs( res );  break;
		/* case DVM_MATH_CEIL : res = par; break; */
		/* case DVM_MATH_FLOOR: res = par; break; */
		}
		if( C && C->type == DAO_INTEGER ){
			C->xInteger.value = res;
		}else{
			value->type = DAO_INTEGER;
			value->xInteger.value = res;
			return DaoValue_Move( value, self->activeValues + vmc->c, dao_type_int ) == 0;
		}
	}else if( A->type && A->type <= DAO_DOUBLE ){
		double par = DaoValue_GetDouble( A );
		double res = 0.0;
		switch( func ){
		case DVM_MATH_ABS  : res = fabs( par );  break;
		case DVM_MATH_ACOS : res = acos( par ); break;
		case DVM_MATH_ASIN : res = asin( par ); break;
		case DVM_MATH_ATAN : res = atan( par ); break;
		case DVM_MATH_CEIL : res = ceil( par ); break;
		case DVM_MATH_COS  : res = cos( par );  break;
		case DVM_MATH_COSH : res = cosh( par ); break;
		case DVM_MATH_EXP  : res = exp( par );  break;
		case DVM_MATH_FLOOR: res = floor( par ); break;
		case DVM_MATH_LOG  : res = log( par );  break;
		case DVM_MATH_RAND : res = par * DaoProcess_Random( self ); break;
		case DVM_MATH_SIN  : res = sin( par );  break;
		case DVM_MATH_SINH : res = sinh( par ); break;
		case DVM_MATH_SQRT : res = sqrt( par ); break;
		case DVM_MATH_TAN  : res = tan( par );  break;
		case DVM_MATH_TANH : res = tanh( par ); break;
		default : return 1;
		}
		if( func == DVM_MATH_RAND ){
			value->type = A->type;
			switch( A->type ){
			case DAO_FLOAT  : value->xFloat.value = res; type = dao_type_float; break;
			case DAO_DOUBLE : value->xDouble.value = res; type = dao_type_double; break;
			}
			return DaoValue_Move( value, self->activeValues + vmc->c, type ) == 0;
		}else if( C && C->type == DAO_DOUBLE ){
			C->xDouble.value = res;
			return 0;
		}else{
			value->type = DAO_DOUBLE;
			value->xDouble.value = res;
			return DaoValue_Move( value, self->activeValues + vmc->c, dao_type_double ) == 0;
		}
	}
	return 1;
}
DaoValue* DaoTypeCast( DaoProcess *proc, DaoType *ct, DaoValue *dA, DaoValue *dC );
int ConvertStringToNumber( DaoProcess *proc, DaoValue *dA, DaoValue *dC );
void DaoProcess_PopValues( DaoProcess *self, int N );
static void* DaoType_DownCastCxxData( DaoType *self, DaoType *totype, void *data )
{
	daoint i, n;
	if( self == totype || totype == NULL || data == NULL ) return data;
	for(i=0,n=totype->bases->size; i<n; i++){
		void *p = DaoType_DownCastCxxData( self, totype->bases->items.pType[i], data );
		if( p ){
			if( totype->typer->casts[i] ) return (*totype->typer->casts[i])( p, 1 );;
			return p;
		}
	}
	return NULL;
}
void DaoProcess_DoCast( DaoProcess *self, DaoVmCode *vmc )
{
	int i, n, mt, mt2;
	int top = self->factory->size;
	DaoType *at, *ct = self->activeTypes[ vmc->c ];
	DaoValue *va = self->activeValues[ vmc->a ];
	DaoValue *vc = self->activeValues[ vmc->c ];
	DaoValue **vc2 = self->activeValues + vmc->c;
	DaoRoutine *meth;
	DNode *node;

	if( va == NULL ){
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "operate on none object" );
		return;
	}
	if( ct == NULL || ct->tid == DAO_UDT || ct->tid == DAO_ANY ) goto FastCasting;
	if( va->type == ct->tid && ct->tid <= DAO_STRING ) goto FastCasting;

	if( vc && vc->type == ct->tid && va->type <= DAO_STRING ){
		if( va->type == ct->tid ) goto FastCasting;
		if( va->type == DAO_STRING ){
			if( ConvertStringToNumber( self, va, vc ) == 0 ) goto FailConversion;
			return;
		}
		switch( ct->tid ){
		case DAO_INTEGER : vc->xInteger.value = DaoValue_GetInteger( va ); return;
		case DAO_FLOAT   : vc->xFloat.value = DaoValue_GetFloat( va ); return;
		case DAO_DOUBLE  : vc->xDouble.value = DaoValue_GetDouble( va ); return;
		case DAO_COMPLEX : vc->xComplex.value = DaoValue_GetComplex( va ); return;
		case DAO_LONG    : DaoValue_GetLong( va, vc->xLong.value ); return;
		case DAO_STRING  : DaoValue_GetString( va, vc->xString.data ); return;
		}
	}

	if( ct->tid == DAO_ENUM && (vc == NULL || vc->type != DAO_ENUM) ){
		DaoEnum *E = DaoEnum_New( NULL, 0 );
		GC_ShiftRC( E, vc );
		*vc2 = vc = (DaoValue*) E;
	}
	if( ct->tid == DAO_ENUM && va->type == DAO_ENUM ){
		DaoEnum_SetType( & vc->xEnum, ct );
		if( DaoEnum_SetValue( & vc->xEnum, & va->xEnum, NULL ) ==0 ) goto FailConversion;
		return;
	}else if( ct->tid == DAO_ENUM && va->type == DAO_INTEGER ){
		if( ct->mapNames == NULL ) goto FailConversion;
		for(node=DMap_First(ct->mapNames);node;node=DMap_Next(ct->mapNames,node)){
			if( node->value.pInt == va->xInteger.value ) break;
		}
		if( node == NULL ) goto FailConversion;
		DaoEnum_SetType( & vc->xEnum, ct );
		vc->xEnum.value = node->value.pInt;
		return;
	}else if( ct->tid == DAO_ENUM && va->type == DAO_STRING ){
		if( ct->mapNames == NULL ) goto FailConversion;
		node = DMap_Find( ct->mapNames, va->xString.data );
		if( node == NULL ) goto FailConversion;
		DaoEnum_SetType( & vc->xEnum, ct );
		vc->xEnum.value = node->value.pInt;
		return;
	}else if( ct->tid == DAO_ENUM ){
		goto FailConversion;
	}

	if( ct->tid == DAO_VARIANT ){
		at = NULL;
		mt = DAO_MT_NOT;
		for(i=0,n=ct->nested->size; i<n; i++){
			DaoType *tp = ct->nested->items.pType[i];
			mt2 = DaoType_MatchValue( tp, va, NULL );
			if( mt2 > mt ){
				mt = mt2;
				at = tp;
			}
		}
		if( at == NULL ) goto FailConversion;
		ct = at;
	}
	if( ct->tid == DAO_INTERFACE ){
		switch( va->type ){
		case DAO_OBJECT  :
			va = (DaoValue*) va->xObject.rootObject;
			break;
		case DAO_CSTRUCT :
		case DAO_CDATA   :
			if( va->xCstruct.object ) va = (DaoValue*) va->xCstruct.object->rootObject;
			break;
		}
		at = DaoNamespace_GetType( self->activeNamespace, va );
		/* automatic binding when casted to an interface: */
		mt = DaoInterface_BindTo( & ct->aux->xInterface, at, NULL );
	}
	mt = DaoType_MatchValue( ct, va, NULL );
	/* printf( "mt = %i, ct = %s\n", mt, ct->name->mbs ); */
	if( mt == DAO_MT_EQ || (mt && ct->tid == DAO_INTERFACE) ){
		DaoValue_Copy( va, vc2 );
		return;
	}
	if( va->type == DAO_OBJECT ){
		DaoClass *scope = self->activeObject ? self->activeObject->defClass : NULL;
		DaoValue *tpar = (DaoValue*) ct;
		meth = DaoClass_FindOperator( va->xObject.defClass, "cast", scope );
		if( meth && DaoProcess_PushCallable( self, meth, va, & tpar, 1 ) ==0 ) return;
	}else if( va->type == DAO_CSTRUCT || va->type == DAO_CDATA ){
		DaoValue *tpar = (DaoValue*) ct;
		if( DaoType_MatchTo( va->xCdata.ctype, ct, NULL ) ){ /* up casting: */
			/*
			// No real casting here. C codes should use DaoValue_TryCastCdata(),
			// or DaoCdata_CastData() to do the real casting on the C data pointer.
			*/
			goto FastCasting;
		}else if( va->type == DAO_CDATA && DaoType_MatchTo( ct, va->xCdata.ctype, NULL ) ){
			/* down casting: */
			void *data = DaoType_DownCastCxxData( va->xCdata.ctype, ct, va->xCdata.data );
			if( data ){
				va = (DaoValue*) DaoCdata_Wrap( ct, data );
				goto FastCasting;
			}
		}
		meth = DaoType_FindFunctionMBS( va->xCdata.ctype, "cast" );
		if( meth && DaoProcess_PushCallable( self, meth, va, & tpar, 1 ) ==0 ) return;
	}
NormalCasting:
	va = DaoTypeCast( self, ct, va, vc );
	if( va && va->type ) DaoValue_Copy( va, vc2 );
	DaoProcess_PopValues( self, self->factory->size - top );
	if( va == NULL || va->type == 0 ) goto FailConversion;
	return;
FastCasting:
	GC_ShiftRC( va, vc );
	*vc2 = va;
	return;
FailConversion :
	at = DaoNamespace_GetType( self->activeNamespace, self->activeValues[ vmc->a ] );
	DaoProcess_RaiseTypeError( self, at, ct, "casting" );
}
#ifdef DAO_WITH_CONCURRENT
static int DaoProcess_TryAsynCall( DaoProcess *self, DaoVmCode *vmc )
{
	DaoStackFrame *frame = self->topFrame;
	DaoStackFrame *prev = frame->prev;
	if( vmc->b & DAO_CALL_ASYNC ){
		DaoCallServer_AddCall( self );
		self->status = DAO_PROCESS_RUNNING;
		return 1;
	}
	if( vmc->code != DVM_MCALL ) return 0;
	if( frame->object && frame->object->isAsync ){
		if( prev->object == NULL || frame->object->rootObject != prev->object->rootObject ){
			DaoCallServer_AddCall( self );
			self->status = DAO_PROCESS_RUNNING;
			return 1;
		}
	}
	return 0;
}
#endif
static int DaoProcess_InitBase( DaoProcess *self, DaoVmCode *vmc, DaoValue *caller )
{
	if( (vmc->b & DAO_CALL_INIT) && self->activeObject ){
		DaoClass *klass = self->activeObject->defClass;
		int init = self->activeRoutine->attribs & DAO_ROUT_INITOR;
		if( self->activeRoutine->routHost == klass->objType && init ){
			return klass->parent == caller;
		}
	}
	return 0;
}
static void DaoProcess_PrepareCall( DaoProcess *self, DaoRoutine *rout,
		DaoValue *O, DaoValue *P[], int N, DaoVmCode *vmc, int noasync )
{
	DaoRoutine *rout2 = rout;
	int need_self = rout->routType->attrib & DAO_TYPE_SELF;
	rout = DaoProcess_PassParams( self, rout, NULL, O, P, N, vmc->code );
	if( rout == NULL ){
		DaoProcess_RaiseException( self, DAO_ERROR_PARAM, "not matched (passing)" );
		DaoProcess_ShowCallError( self, rout2, O, P, N, vmc->code|((int)vmc->b<<16) );
		return;
	}
	if( need_self && rout->routHost && rout->routHost->tid == DAO_OBJECT ){
		if( O == NULL && N && P[0]->type == DAO_OBJECT ) O = P[0];
		if( O ) O = DaoObject_CastToBase( O->xObject.rootObject, rout->routHost );
		if( O == NULL && N && P[0]->type == DAO_PAR_NAMED ){ /* Check explicit self parameter: */
			DaoNameValue *nameva = (DaoNameValue*)P[0];
			if( nameva->value && nameva->value->type == DAO_OBJECT )
				if( nameva->unitype->attrib & DAO_TYPE_SELFNAMED ){
					O = DaoObject_CastToBase( nameva->value->xObject.rootObject, rout->routHost );
				}
		}
		if( O == NULL ){
			DaoProcess_RaiseException( self, DAO_ERROR, "self object is null" );
			return;
		}else if( O == O->xObject.defClass->objType->value ){
			DaoProcess_RaiseException( self, DAO_ERROR, "self object is the default object" );
			return;
		}
	}
	/* no tail call optimization when there is deferred code blocks: */
	if( (vmc->b & DAO_CALL_TAIL) && self->defers->size == self->topFrame->deferBase ){
		int async = vmc->b & DAO_CALL_ASYNC;
		DaoObject *root = NULL;
		switch( O ? O->type : 0 ){
		case DAO_CDATA   :
		case DAO_CSTRUCT :
			if( O->xCstruct.object ) root = O->xCstruct.object->rootObject;
			break;
		case DAO_OBJECT  : root = O->xObject.rootObject; break;
		}
		if( root ) async |= root->isAsync;
		/* No tail call optimization for possible asynchronous calls: */
		/* No tail call optimization in constructors etc.: */
		/* (self->topFrame->state>>1): get rid of the DVM_FRAME_RUNNING flag: */
		if( async == 0 && (self->topFrame->state>>1) == 0 && daoConfig.optimize ){
			/* No optimization if the tail call has a return type different from the current: */
			if( rout->routType->aux == self->activeRoutine->routType->aux )
				DaoProcess_PopFrame( self );
		}
	}
	DaoProcess_PushRoutine( self, rout, DaoValue_CastObject( O ) );//, code );
	if( noasync ) return;
#ifdef DAO_WITH_CONCURRENT
	DaoProcess_TryAsynCall( self, vmc );
#endif
}
static void DaoProcess_DoCxxCall( DaoProcess *self, DaoVmCode *vmc,
		DaoType *hostype, DaoRoutine *func, DaoValue *selfpar, DaoValue *P[], int N, int noasync )
{
	DaoRoutine *rout = func;
	DaoVmSpace *vmspace = self->vmSpace;
	DaoValue *caller = self->activeValues[ vmc->a ];
	int status, code = vmc->code;
	int codemode = code|((int)vmc->b<<16);

	func = DaoRoutine_ResolveX( func, selfpar, P, N, codemode );
	if( func == NULL ){
		DaoProcess_ShowCallError( self, rout, selfpar, P, N, codemode );
		return;
	}
	if( (vmspace->options & DAO_OPTION_SAFE) && func->nameSpace != vmspace->nsInternal ){
		/* normally this condition will not be satisfied.
		 * it is possible only if the safe mode is set in C codes
		 * by embedding or extending. */
		DaoProcess_RaiseException( self, DAO_ERROR, "not permitted" );
		return;
	}
	if( (func = DaoProcess_PassParams( self, func, hostype, selfpar, P, N, code )) == NULL ){
		DaoProcess_ShowCallError( self, rout, selfpar, P, N, codemode );
		return;
	}
	DaoProcess_PushFunction( self, func );
#ifdef DAO_WITH_CONCURRENT
	if( noasync == 0 && DaoProcess_TryAsynCall( self, vmc ) ) return;
#endif
#if 0
	if( caller->type == DAO_CTYPE ){
		DaoType *retype = caller->xCtype.cdtype;
		printf( ">>>>>>>>>>>>> %s %s\n", retype->name->mbs, caller->xCdata.ctype->name->mbs );
		GC_ShiftRC( retype, self->topFrame->retype );
		self->topFrame->retype = retype;
	}
#endif
	DaoProcess_CallFunction( self, func, self->stackValues + self->topFrame->stackBase, self->parCount );
	status = self->status;
	DaoProcess_PopFrame( self );

	if( status == DAO_PROCESS_SUSPENDED ) self->status = status;
}
static void DaoProcess_DoNewCall( DaoProcess *self, DaoVmCode *vmc,
		DaoClass *klass, DaoValue *selfpar, DaoValue *params[], int npar )
{
	DaoValue *ret;
	DaoRoutine *rout;
	DaoRoutine *routines = klass->classRoutines;
	DaoObject *obj, *othis = NULL, *onew = NULL;
	int i, code = vmc->code;
	int codemode = code | (vmc->b<<16);
	int initbase = DaoProcess_InitBase( self, vmc, (DaoValue*) klass );
	if( initbase ){
		othis = self->activeObject;
	}else{
		othis = onew = DaoObject_New( klass );
		if( vmc->b & DAO_CALL_ASYNC ) onew->isAsync = 1;
	}
	rout = DaoRoutine_ResolveX( routines, selfpar, params, npar, codemode );
	if( rout == NULL ){
		selfpar = (DaoValue*) othis;
		rout = DaoRoutine_ResolveX( routines, selfpar, params, npar, codemode );
	}
	if( rout == NULL ) goto InvalidParameter;
	if( rout->pFunc ){
		rout = DaoProcess_PassParams( self, rout, klass->objType, selfpar, params, npar, vmc->code );
		if( rout == NULL ) goto InvalidParameter;
		DaoProcess_PushFunction( self, rout );
		DaoProcess_SetActiveFrame( self, self->firstFrame ); /* return value in stackValues[0] */
		self->topFrame->active = self->firstFrame;
		DaoProcess_CallFunction( self, rout, self->stackValues + self->topFrame->stackBase, self->parCount );
		DaoProcess_PopFrame( self );

		ret = self->stackValues[0];
		if( ret && (ret->type == DAO_CDATA || ret->type == DAO_CSTRUCT) ){
			DaoCdata *cdata = & self->stackValues[0]->xCdata;
			DaoObject_SetParentCdata( othis, cdata );
			GC_ShiftRC( othis, cdata->object );
			cdata->object = othis;
		}
		DaoProcess_PutValue( self, (DaoValue*) othis );
	}else{
		obj = othis;
		if( initbase ){
			obj = (DaoObject*) DaoObject_CastToBase( obj, rout->routHost );
			if( obj->isInited ) return;
		}
		obj->isInited = 1;
		DaoProcess_PrepareCall( self, rout, (DaoValue*) obj, params, npar, vmc, 1 );
		if( self->exceptions->size ) goto DeleteObject;
	}
	return;
InvalidParameter:
	DaoProcess_ShowCallError( self, routines, selfpar, params, npar, DVM_CALL );
DeleteObject:
	if( onew ){ GC_IncRC( onew ); GC_DecRC( onew ); }
}
void DaoProcess_DoCall2( DaoProcess *self, DaoVmCode *vmc, DaoValue *caller, DaoValue *selfpar, DaoValue *params[], int npar )
{
	int i, sup = 0;
	int code = vmc->code;
	int codemode = code | (vmc->b<<16);
	DaoStackFrame *topFrame = self->topFrame;
	DaoRoutine *rout, *rout2;
	DArray *array, *bindings;

	if( caller->type == DAO_ROUTINE ){
		rout = (DaoRoutine*) caller;
		if( rout->pFunc ){
			DaoProcess_DoCxxCall( self, vmc, NULL, rout, selfpar, params, npar, 0 );
			return;
		}else if( rout->overloads == NULL && rout->body == NULL ){  /* function curry: */
			DaoValue *caller = (DaoValue*) rout->original;
			DaoVmCode vmc2 = *vmc;
			if( rout->original == NULL ){
				DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "abstract routine not callable" );
				return;
			}
			if( rout->original->routType->attrib & DAO_TYPE_SELF ) vmc2.code = DVM_MCALL;
			array = DArray_New(0);
			bindings = & rout->routConsts->items;
			for(i=0; i<bindings->size; i++) DArray_Append( array, bindings->items.pValue[i] );
			for(i=0; i<npar; i++) DArray_Append( array, params[i] );
			DaoProcess_DoCall2( self, & vmc2, caller, NULL, array->items.pValue, array->size );
			DArray_Delete( array );
			return;
		}
		rout = DaoRoutine_ResolveX( rout, selfpar, params, npar, codemode );
		if( rout == NULL ){
			rout2 = (DaoRoutine*) caller;
			goto InvalidParameter;
		}
		if( rout->pFunc ){
			DaoProcess_DoCxxCall( self, vmc, NULL, rout, selfpar, params, npar, 0 );
		}else{
			if( rout->attribs & DAO_ROUT_DECORATOR ){
#ifdef DAO_WITH_DECORATOR
				DaoRoutine *drout = (DaoRoutine*) rout;
				if( params[0]->type != DAO_ROUTINE ){
					DaoProcess_RaiseException( self, DAO_INVALID_FUNCTION_DECORATION, NULL );
					return;
				}
				drout = DaoRoutine_Decorate( & params[0]->xRoutine, drout, params, npar, 0 );
				DaoProcess_PutValue( self, (DaoValue*) drout );
#else
				DaoProcess_RaiseException( self, DAO_ERROR, getCtInfo( DAO_DISABLED_DECORATOR ) );
#endif
				return;
			}
			DaoProcess_PrepareCall( self, rout, selfpar, params, npar, vmc, 0 );
		}
	}else if( caller->type == DAO_CLASS ){
		DaoProcess_DoNewCall( self, vmc, & caller->xClass, selfpar, params, npar );
		if( self->topFrame != topFrame ){
			GC_ShiftRC( caller->xClass.objType, self->topFrame->retype );
			self->topFrame->retype = caller->xClass.objType;
		}
	}else if( caller->type == DAO_OBJECT ){
		DaoClass *host = self->activeObject ? self->activeObject->defClass : NULL;
		rout = rout2 = DaoClass_FindOperator( caller->xObject.defClass, "()", host );
		if( rout == NULL ){
			DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "class instance not callable" );
			return;
		}
		rout = DaoRoutine_ResolveX( rout, caller, params, npar, codemode );
		if( rout == NULL ) goto InvalidParameter;
		if( rout->pFunc ){
			DaoProcess_DoCxxCall( self, vmc, NULL, rout, caller, params, npar, 0 );
		}else if( rout->type == DAO_ROUTINE ){
			DaoProcess_PrepareCall( self, rout, selfpar, params, npar, vmc, 0 );
		}
	}else if( caller->type == DAO_CTYPE ){
		DaoType *type = caller->xCdata.ctype;
		rout = rout2 = DaoType_FindFunction( type, type->name );
		if( rout == NULL ){
			DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "C type not callable" );
			return;
		}
		rout = DaoRoutine_ResolveX( rout, selfpar, params, npar, codemode );
		if( rout == NULL /*|| rout->pFunc == NULL*/ ) goto InvalidParameter;
		DaoProcess_DoCxxCall( self, vmc, caller->xCdata.ctype, rout, selfpar, params, npar, 1 );
		if( self->exceptions->size ) return;

		sup = DaoProcess_InitBase( self, vmc, caller );
		//printf( "sup = %i\n", sup );
		if( caller->type == DAO_CTYPE && sup ){
			DaoCdata *cdata = & self->activeValues[ vmc->c ]->xCdata;
			if( cdata && (cdata->type == DAO_CDATA || cdata->type == DAO_CSTRUCT) ){
				//printf( "%p %p %p\n", cdata, cdata->object, self->activeObject->rootObject );
				GC_ShiftRC( cdata, self->activeObject->parent );
				self->activeObject->parent = (DaoValue*) cdata;
				GC_ShiftRC( self->activeObject->rootObject, cdata->object );
				cdata->object = self->activeObject->rootObject;
			}
		}
	}else if( caller->type == DAO_CDATA || caller->type == DAO_CSTRUCT ){
		rout = rout2 = DaoType_FindFunctionMBS( caller->xCdata.ctype, "()" );
		if( rout == NULL ){
			DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "C object not callable" );
			return;
		}
		rout = DaoRoutine_ResolveX( rout, selfpar, params, npar, codemode );
		if( rout == NULL /*|| rout->pFunc == NULL*/ ) goto InvalidParameter;
		DaoProcess_DoCxxCall( self, vmc, NULL, rout, selfpar, params, npar, 0 );
	}else{
		DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "object not callable" );
	}
	return;
InvalidParameter:
	DaoProcess_ShowCallError( self, rout2, selfpar, params, npar, codemode );
}
static void DaoProcess_FastPassParams( DaoProcess *self, DaoValue *params[], int npar )
{
	int i;
	DaoValue **dests = self->stackValues + self->topFrame->stackBase;
	for(i=0; i<npar; ++i){
		if( dests[i] && dests[i]->xBase.refCount == 1 && params[i]->type == dests[i]->type ){
			switch( params[i]->type ){
			case DAO_INTEGER :
				dests[i]->xInteger.value = params[i]->xInteger.value;
				break;
			case DAO_FLOAT :
				dests[i]->xFloat.value = params[i]->xFloat.value;
				break;
			case DAO_DOUBLE :
				dests[i]->xDouble.value = params[i]->xDouble.value;
				break;
			case DAO_COMPLEX :
				dests[i]->xComplex.value = params[i]->xComplex.value;
				break;
#ifdef DAO_WITH_LONGINT
			case DAO_LONG :
				DLong_Move( dests[i]->xLong.value, params[i]->xLong.value );
				break;
#endif
			case DAO_STRING :
				DString_Assign( dests[i]->xString.data, params[i]->xString.data );
				break;
			default :
				GC_ShiftRC( params[i], dests[i] );
				dests[i] = params[i];
				break;
			}
		}else if( params[i]->type >= DAO_ARRAY ){
			GC_ShiftRC( params[i], dests[i] );
			dests[i] = params[i];
		}else{
			DaoValue_CopyX( params[i], dests + i, self->cache );
		}
	}
}
void DaoProcess_DoCall( DaoProcess *self, DaoVmCode *vmc )
{
	int status;
	int mode = vmc->b;
	int npar = vmc->b & 0xff;
	int mcall = vmc->code == DVM_MCALL;
	DaoValue *parbuf[DAO_MAX_PARAM+1];
	DaoValue *selfpar = NULL;
	DaoValue *caller = self->activeValues[ vmc->a ];
	DaoValue **params = self->activeValues + vmc->a;
	DaoRoutine *rout, *rout2 = NULL;
	DaoType *retype;

	self->activeCode = vmc;
	if( (mode & DAO_CALL_FAST) && caller->xRoutine.overloads == NULL ){
		rout = (DaoRoutine*) caller;
		if( rout->pFunc ){
			DaoStackFrame *frame = DaoProcess_PushFrame( self, rout->parCount );
			DaoValue **values = self->stackValues + frame->stackBase;
			GC_ShiftRC( rout, frame->routine );
			frame->routine = rout;
			frame->active = frame->prev->active;
			self->status = DAO_PROCESS_STACKED;
			DaoProcess_FastPassParams( self, self->activeValues + vmc->a + 1, npar );
			DaoProcess_CallFunction( self, rout, values, rout->parCount );
			status = self->status;
			DaoProcess_PopFrame( self );
			if( status == DAO_PROCESS_SUSPENDED ) self->status = status;
		}else{
			DaoStackFrame *frame = DaoProcess_PushFrame( self, rout->body->regCount );
			frame->active = frame;
			self->status = DAO_PROCESS_STACKED;
			DaoProcess_InitTopFrame( self, rout, NULL );
			DaoProcess_FastPassParams( self, self->activeValues + vmc->a + 1, npar );
		}
		return;
	}

	if( caller == NULL || caller->type ==0 ){
		DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "none object not callable" );
		return;
	}
	if( self->activeObject && mcall == 0 ) selfpar = (DaoValue*) self->activeObject;
	if( mode & DAO_CALL_NOSELF ) selfpar = NULL;
	if( (mode & DAO_CALL_EXPAR) && npar > mcall && params[npar]->type == DAO_TUPLE ){
		DaoTuple *tup = & params[npar]->xTuple;
		DArray *ts = tup->unitype->nested;
		int i, m, n = -1;
		/* Handle explicit "self" argument: */
		if( ts->size && (ts->items.pType[0]->attrib & DAO_TYPE_SELFNAMED) ) selfpar = NULL;
		for(i=0; i<npar+1; ++i) parbuf[++n] = params[i];
		for(i=0,m=tup->size; i<m; ++i) parbuf[n++] = tup->items[i];
		params = parbuf;
		npar = n - 1;
	}
	DaoProcess_DoCall2( self, vmc, caller, selfpar, params + 1, npar );
	/*
	// Put a none value in case that a routine is inferred to return none value,
	// and actually returns no value. Because a none value may be expected by
	// other expression.
	*/
	vmc = self->activeCode; /* tail call optimization may have changed this; */
	retype = self->activeTypes[ vmc->c ];
	if( retype == NULL || (retype->tid == DAO_VALTYPE && retype->aux->type == DAO_NONE) ){
		DaoValue *revalue = self->activeValues[vmc->c];
		if( revalue != NULL && revalue->type != DAO_NONE ){
			GC_ShiftRC( dao_none_value, revalue );
			self->activeValues[vmc->c] = dao_none_value;
		}
#ifdef DEBUG
		/* Make sure the return type is set for constant folding: */
		assert( retype != NULL || !(self->trait & DAO_VALUE_CONST) );
#endif
	}
}

daoint DaoArray_SliceSize( DaoArray *self );
int DaoObject_InvokeMethod( DaoObject *self, DaoObject *othis, DaoProcess *proc,
		DString *name, DaoValue *P[], int N, int ignore_return, int execute );
static void DaoProcess_InitIter( DaoProcess *self, DaoVmCode *vmc )
{
	DString *name = self->mbstring;
	DaoValue *va = self->activeValues[ vmc->a ];
	DaoValue *vc = self->activeValues[ vmc->c ];
	DaoType *type = DaoNamespace_GetType( self->activeNamespace, va );
	DaoInteger *index;
	DaoTuple *iter;
	int rc = 1;

	if( va == NULL || va->type == 0 ) return;

	if( vc == NULL || vc->type != DAO_TUPLE || vc->xTuple.unitype != dao_type_for_iterator ){
		vc = (DaoValue*) DaoProcess_PutTuple( self, 0 );
	}

	iter = & vc->xTuple;
	iter->items[0]->xInteger.value = 0;
	DaoTuple_SetItem( iter, dao_none_value, 1 );

	index = DaoInteger_New(0);
	if( va->type == DAO_STRING ){
		iter->items[0]->xInteger.value = va->xString.data->size >0;
		DaoValue_Copy( (DaoValue*) index, iter->items + 1 );
#ifdef DAO_WITH_NUMARRAY
	}else if( va->type == DAO_ARRAY ){
		iter->items[0]->xInteger.value = DaoArray_SliceSize( (DaoArray*) va ) >0;
		DaoValue_Copy( (DaoValue*) index, iter->items + 1 );
#endif
	}else if( va->type == DAO_LIST ){
		iter->items[0]->xInteger.value = va->xList.items.size >0;
		DaoValue_Copy( (DaoValue*) index, iter->items + 1 );
	}else if( va->type == DAO_MAP ){
		DNode *node = DMap_First( va->xMap.items );
		DaoValue **data = iter->items;
		data[0]->xInteger.value = va->xMap.items->size >0;
		if( data[1]->type != DAO_CDATA || data[1]->xCdata.ctype != dao_default_cdata.ctype ){
			DaoCdata *it = DaoCdata_New( dao_default_cdata.ctype, node );
			GC_ShiftRC( it, data[1] );
			data[1] = (DaoValue*) it;
		}else{
			data[1]->xCdata.data = node;
		}
	}else if( va->type == DAO_TUPLE ){
		iter->items[0]->xInteger.value = va->xTuple.size >0;
		DaoValue_Copy( (DaoValue*) index, iter->items + 1 );
	}else{
		DString_SetMBS( name, "__for_iterator__" );
		if( va->type == DAO_OBJECT ){
			rc = DaoObject_InvokeMethod( & va->xObject, NULL, self, name, & vc, 1, 1, 0 );
		}else{
			DaoRoutine *meth = DaoType_FindFunction( type, name );
			if( meth ) rc = DaoProcess_Call( self, meth, va, &vc, 1 );
		}
		if( rc ) DaoProcess_RaiseException( self, DAO_ERROR_FIELD_NOTEXIST, name->mbs );
	}
	dao_free( index );
}
static void DaoProcess_TestIter( DaoProcess *self, DaoVmCode *vmc )
{
	int i, res = 1;
	for(i=0; i<vmc->b; ++i){
		DaoTuple *iter = (DaoTuple*) self->activeValues[vmc->a+i];
		res &= iter->items[0]->xInteger.value != 0;
	}
	self->activeValues[vmc->c]->xInteger.value = res;
}
void DaoProcess_DoIter( DaoProcess *self, DaoVmCode *vmc )
{
	if( vmc->b ){
		DaoProcess_TestIter( self, vmc );
	}else{
		DaoProcess_InitIter( self, vmc );
	}
}


void DaoProcess_DoList(  DaoProcess *self, DaoVmCode *vmc )
{
	DaoNamespace *ns = self->activeNamespace;
	DaoValue **regValues = self->activeValues;
	const int bval = vmc->b;
	const ushort_t opA = vmc->a;
	int i;

	DaoList *list = DaoProcess_GetList( self, vmc );
	DArray_Resize( & list->items, bval, NULL );
	if( bval >0 && self->activeTypes[ vmc->c ] ==NULL ){
		DaoType *abtp = DaoNamespace_GetType( ns, regValues[opA] );
		DaoType *t = DaoNamespace_MakeType( ns, "list", DAO_LIST, NULL, & abtp, 1 );
		GC_ShiftRC( t, list->unitype );
		list->unitype = t;
	}
	if( vmc->b && list->unitype && list->unitype->isempty2 ){
		GC_ShiftRC( dao_list_any, list->unitype );
		list->unitype = dao_list_any;
	}
	for( i=0; i<bval; i++){
		if( DaoList_SetItem( list, regValues[opA+i], i ) ){
			DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "invalid items" );
			return;
		}
	}
}
static void DaoProcess_SetVectorValues( DaoProcess *self, DaoArray *a, DaoValue *v[], int N );
void DaoProcess_DoVector( DaoProcess *self, DaoVmCode *vmc )
{
#ifdef DAO_WITH_NUMARRAY
	const ushort_t opA = vmc->a;
	const ushort_t count = vmc->b;
	DaoArray *array = DaoProcess_GetArray( self, vmc );

	if( count && array->etype == DAO_NONE ){
		DaoValue *p = self->activeValues[opA];
		switch( p->type ){
			case DAO_INTEGER :
			case DAO_FLOAT :
			case DAO_DOUBLE :
			case DAO_COMPLEX : array->etype = p->type; break;
			case DAO_ARRAY : array->etype = p->xArray.etype; break;
			default : DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "invalid items" ); return;
		}
	}else if( array->etype == DAO_NONE ){
		array->etype = DAO_FLOAT;
	}
	DaoProcess_SetVectorValues( self, array, self->activeValues + opA, count );
#else
	self->activeCode = vmc;
	DaoProcess_RaiseException( self, DAO_ERROR, getCtInfo( DAO_DISABLED_NUMARRAY ) );
#endif
}
void DaoProcess_SetVectorValues( DaoProcess *self, DaoArray *array, DaoValue *values[], int N )
{
	daoint *dims = NULL;
	daoint i, j, k = 0;
	int m, ndim = 0;

#ifdef DAO_WITH_NUMARRAY
	for( j=0; j<N; j++){
		DaoValue *p = values[j];
		if( p == NULL || p->type == DAO_NONE ) goto InvalidItem;
		if( p->type > DAO_COMPLEX && p->type != DAO_ARRAY ) goto InvalidItem;
		if( p->type == DAO_ARRAY ){
			if( j && dims == NULL ) goto InvalidItem;
		}else{
			if( j && dims ) goto InvalidItem;
			continue;
		}
		if( dims == NULL ){
			ndim = p->xArray.ndim;
			dims = p->xArray.dims;
		}
		if( dims == p->xArray.dims ) continue;
		if( ndim != p->xArray.ndim ) goto InvalidItem;
		for(m=0; m<ndim; m++) if( dims[m] != p->xArray.dims[m] ) goto InvalidItem;
		continue;
InvalidItem:
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "array item type or shape not matching" );
		return;
	}
	if( dims ){
		DaoArray_SetDimCount( array, ndim + 1 );
		array->dims[0] = N;
		memmove( array->dims + 1, dims, ndim*sizeof(daoint) );
		DaoArray_ResizeArray( array, array->dims, ndim + 1 );
	}else{
		DaoArray_ResizeVector( array, N );
	}
	k = 0;
	if( array->etype == DAO_INTEGER ){
		daoint *vals = array->data.i;
		for( j=0; j<N; j++ ){
			DaoValue *p = values[j];
			if( p && p->type == DAO_ARRAY ){
				DaoArray *array2 = & p->xArray;
				for(i=0; i<array2->size; i++){
					vals[k] = DaoArray_GetInteger( array2, i );
					k++;
				}
			}else{
				vals[k] = DaoValue_GetInteger( p );
				k ++;
			}
		}
	}else if( array->etype == DAO_FLOAT ){
		float *vals = array->data.f;
		for( j=0; j<N; j++ ){
			DaoValue *p = values[j];
			if( p && p->type == DAO_ARRAY ){
				DaoArray *array2 = & p->xArray;
				for(i=0; i<array2->size; i++){
					vals[k] = DaoArray_GetFloat( array2, i );
					k++;
				}
			}else{
				vals[k] = DaoValue_GetFloat( p );
				k ++;
			}
		}
	}else if( array->etype == DAO_DOUBLE ){
		double *vals = array->data.d;
		for( j=0; j<N; j++ ){
			DaoValue *p = values[j];
			if( p && p->type == DAO_ARRAY ){
				DaoArray *array2 = & p->xArray;
				for(i=0; i<array2->size; i++){
					vals[k] = DaoArray_GetDouble( array2, i );
					k++;
				}
			}else{
				vals[k] = DaoValue_GetDouble( p );
				k ++;
			}
		}
	}else{
		complex16 *vals = array->data.c;
		for( j=0; j<N; j++ ){
			DaoValue *p = values[j];
			if( p && p->type == DAO_ARRAY ){
				DaoArray *array2 = & p->xArray;
				for(i=0; i<array2->size; i++){
					vals[k] = DaoArray_GetComplex( array2, i );
					k++;
				}
			}else{
				vals[k] = DaoValue_GetComplex( p );
				k ++;
			}
		}
	}
#endif
}
void DaoProcess_DoAPList(  DaoProcess *self, DaoVmCode *vmc )
{
	DaoList *list = DaoProcess_GetList( self, vmc );
	DaoValue **items, **regValues = self->activeValues;
	DaoValue *countValue = regValues[vmc->a + 1 + (vmc->b == 3)];
	DaoValue *initValue = regValues[vmc->a];
	DaoValue *stepValue = vmc->b == 3 ? regValues[vmc->a+1] : NULL;
	daoint i, num = DaoValue_GetInteger( countValue );
	double step = stepValue ? DaoValue_GetDouble( stepValue ) : 0.0;

	self->activeCode = vmc;
	if( countValue->type < DAO_INTEGER || countValue->type > DAO_DOUBLE ){
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "need number" );
		return;
	}
	if( initValue->type < DAO_INTEGER || initValue->type >= DAO_ENUM ){
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "need a number or string as first value" );
		return;
	}
	if( ( self->vmSpace->options & DAO_OPTION_SAFE ) && num > 1000 ){
		DaoProcess_RaiseException( self, DAO_ERROR, "not permitted" );
		return;
	}
	DArray_Resize( & list->items, num, initValue );
	if( num == 0 || stepValue == NULL ) goto SetupType;

	items = list->items.items.pValue;
	switch( initValue->type ){
		case DAO_INTEGER :
		{
			daoint value = initValue->xInteger.value;
			if( stepValue->type == DAO_INTEGER ){
				daoint step = stepValue->xInteger.value;
				for(i=0; i<num; i++, value+=step) items[i]->xInteger.value = value;
			}else{
				for(i=0; i<num; i++, value+=step) items[i]->xInteger.value = value;
			}
			break;
		}
		case DAO_FLOAT :
		{
			double value = initValue->xFloat.value;
			for(i=0; i<num; i++, value+=step) items[i]->xFloat.value = value;
			break;
		}
		case DAO_DOUBLE :
		{
			double value = initValue->xDouble.value;
			for(i=0; i<num; i++, value+=step) items[i]->xDouble.value = value;
			break;
		}
		case DAO_COMPLEX :
		{
			complex16 value = initValue->xComplex.value;
			complex16 step = DaoValue_GetComplex( stepValue );
			for(i=0; i<num; i++){
				items[i]->xComplex.value = value;
				value.real += step.real;
				value.imag += step.imag;
			}
			break;
		}
#ifdef DAO_WITH_LONGINT
		case DAO_LONG :
		{
			DLong *value = initValue->xLong.value;
			DLong *step = NULL, *buf = NULL;
			if( stepValue->type == DAO_LONG ){
				step = stepValue->xLong.value;
			}else{
				step = buf = DLong_New();
				DLong_FromValue( buf, stepValue );
			}
			DLong_Move( items[0]->xLong.value, value );
			for(i=1; i<num; i++) DLong_Add( items[i]->xLong.value, items[i-1]->xLong.value, step );
			if( buf ) DLong_Delete( buf );
			break;
		}
#endif
		case DAO_STRING :
		{
			DString *value = initValue->xString.data;
			DString *one, *step = NULL, *buf = NULL;
			if( stepValue->type == DAO_STRING ){
				step = stepValue->xString.data;
			}else{
				step = buf = DString_New( value->mbs != NULL );
				DaoValue_GetString( stepValue, buf );
			}
			one = DString_Copy( value );
			for(i=0; i<num; i++){
				DString_Assign( items[i]->xString.data, one );
				if( step ) DString_Append( one, step );
			}
			DString_Delete( one );
			if( buf ) DString_Delete( buf );
			break;
		}
		case DAO_ARRAY :
			/* XXX */
			break;
		default: break;
	}
SetupType:
	if( self->activeTypes[ vmc->c ] == NULL ){
		DaoNamespace *ns = self->activeNamespace;
		DaoType *et = DaoNamespace_GetType( ns, initValue );
		DaoType *tp = DaoNamespace_MakeType( ns, "list", DAO_LIST, NULL, & et, et !=NULL );
		GC_ShiftRC( tp, list->unitype );
		list->unitype = tp;
	}
}
void DaoProcess_DoAPVector( DaoProcess *self, DaoVmCode *vmc )
{
#ifdef DAO_WITH_NUMARRAY
	DaoArray *array = NULL;
	DaoValue **regValues = self->activeValues;
	DaoValue *countValue = regValues[vmc->a + 1 + (vmc->b == 3)];
	DaoValue *initValue = regValues[vmc->a];
	DaoValue *stepValue = vmc->b == 3 ? regValues[vmc->a+1] : NULL;
	double step = stepValue ? DaoValue_GetDouble( stepValue ) : 0.0;
	daoint num = DaoValue_GetInteger( countValue );
	daoint i, j, k, m, N, S, transvec = 0; /* transposed vector */

	self->activeCode = vmc;
	if( countValue->type < DAO_INTEGER || countValue->type > DAO_DOUBLE ){
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "need number" );
		return;
	}
	if( ( self->vmSpace->options & DAO_OPTION_SAFE ) && num > 1000 ){
		DaoProcess_RaiseException( self, DAO_ERROR, "not permitted" );
		return;
	}
	array = DaoProcess_GetArray( self, vmc );
	if( array->etype == DAO_NONE ) array->etype = initValue->type;
	DaoArray_ResizeVector( array, num );

	if( initValue->type == DAO_ARRAY ){
		DaoArray *a0 = (DaoArray*) initValue;
		DaoArray_SetNumType( array, a0->etype );
		if( a0->ndim == 2 && (a0->dims[0] == 1 || a0->dims[1] == 1) ){
			DaoArray_SetDimCount( array, 2 );
			memmove( array->dims, a0->dims, 2*sizeof(daoint) );
			array->dims[ a0->dims[0] > 1 ] = num;
			transvec = a0->dims[0] > 1;
		}else{
			DaoArray_SetDimCount( array, a0->ndim + 1 );
			array->dims[0] = num;
			memmove( array->dims + 1, a0->dims, a0->ndim*sizeof(daoint) );
		}
		DaoArray_ResizeArray( array, array->dims, array->ndim );
		S = a0->size;
		N = num * a0->size;
		if( stepValue && stepValue->type == DAO_ARRAY ){
			DaoArray *a1 = (DaoArray*) stepValue;
			const char* const msg[2] = { "invalid step array", "unmatched init and step array" };
			int d, error = -1;
			if( a0->etype <= DAO_DOUBLE && a1->etype >= DAO_COMPLEX ){
				error = 0;
			}else if( a1->ndim != a0->ndim ){
				error = 1;
			}else{
				for(d=0; d<a0->ndim; d++){
					if( a0->dims[d] != a1->dims[d] ){
						error = 1;
						break;
					}
				}
			}
			if( error >=0 ){
				DaoProcess_RaiseException( self, DAO_ERROR_VALUE, msg[error] );
				return;
			}
			for(i=0, m = 0, j=0, k = 0; i<N; i++, m=i, j=i%S, k=i/S){
				if( transvec ) m = j * num + k;
				switch( a0->etype ){
				case DAO_INTEGER :
					if( a1->etype == DAO_INTEGER ){
						array->data.i[m] = a0->data.i[j] + k*a1->data.i[j];
					}else{
						array->data.i[m] = a0->data.i[j] + k*DaoArray_GetDouble( a1, j );
					}
					break;
				case DAO_FLOAT :
					array->data.f[m] = a0->data.f[j] + k*DaoArray_GetDouble( a1, j );
					break;
				case DAO_DOUBLE :
					array->data.d[m] = a0->data.d[j] + k*DaoArray_GetDouble( a1, j );
					break;
				case DAO_COMPLEX :
					if( a1->etype == DAO_COMPLEX ){
						array->data.c[m].real = a0->data.c[j].real + k*a1->data.c[j].real;
						array->data.c[m].imag = a0->data.c[j].imag + k*a1->data.c[j].imag;
					}else{
						array->data.c[m].real = a0->data.c[j].real + k*DaoArray_GetDouble( a1, j );
						array->data.c[m].imag = a0->data.c[j].imag;
					}
					break;
				default : break;
				}
			}
		}else{
			int istep = stepValue && stepValue->type == DAO_INTEGER;
			daoint intstep = istep ? stepValue->xInteger.value : 0;
			complex16 cstep = { 0.0, 0.0 };
			if( stepValue && stepValue->type > DAO_NONE && stepValue->type <= DAO_COMPLEX ){
				cstep = DaoValue_GetComplex( stepValue );
			}
			for(i=0, m = 0, j=0, k = 0; i<N; i++, m=i, j=i%S, k=i/S){
				if( transvec ) m = j * num + k;
				switch( a0->etype ){
				case DAO_INTEGER :
					array->data.i[m] = a0->data.i[j] + (istep ? k * intstep : (daoint)(k * step));
					break;
				case DAO_FLOAT :
					array->data.f[m] = a0->data.f[j] + k * step;
					break;
				case DAO_DOUBLE :
					array->data.d[m] = a0->data.d[j] + k * step;
					break;
				case DAO_COMPLEX :
					array->data.c[m].real = a0->data.c[j].real + k * cstep.real;
					array->data.c[m].imag = a0->data.c[j].imag + k * cstep.imag;
					break;
				}
			}
		}
		return;
	}

	switch( array->etype ){
		case DAO_INTEGER :
		{
			double value;
			if( stepValue == NULL || stepValue->type == DAO_INTEGER ){
				if( initValue->type == DAO_INTEGER ){
					daoint value = initValue->xInteger.value;
					daoint step = stepValue ? stepValue->xInteger.value : 0;
					for(i=0; i<num; i++, value+=step) array->data.i[i] = value;
					break;
				}
			}
			value = DaoValue_GetDouble( initValue );
			for(i=0; i<num; i++, value+=step) array->data.i[i] = (daoint)value;
			break;
		}
		case DAO_FLOAT :
		{
			double value = DaoValue_GetDouble( initValue );
			for(i=0; i<num; i++, value+=step) array->data.f[i] = value;
			break;
		}
		case DAO_DOUBLE :
		{
			double value = DaoValue_GetDouble( initValue );
			for(i=0; i<num; i++, value+=step) array->data.d[i] = value;
			break;
		}
		case DAO_COMPLEX :
		{
			complex16 value = DaoValue_GetComplex( initValue );
			complex16 step = DaoValue_GetComplex( stepValue ? stepValue : dao_none_value );
			for(i=0; i<num; i++){
				array->data.c[i] = value;
				COM_IP_ADD( value, step );
			}
			break;
		}
		default: break;
	}
	if( ( self->vmSpace->options & DAO_OPTION_SAFE ) && array->size > 5000 ){
		DaoProcess_RaiseException( self, DAO_ERROR, "not permitted" );
		return;
	}
#else
	DaoProcess_RaiseException( self, DAO_ERROR, getCtInfo( DAO_DISABLED_NUMARRAY ) );
#endif
}
void DaoProcess_DoMap( DaoProcess *self, DaoVmCode *vmc )
{
	int i, c;
	const ushort_t opA = vmc->a;
	const ushort_t bval = vmc->b;
	DaoNamespace *ns = self->activeNamespace;
	DaoValue **pp = self->activeValues;
	DaoMap *map = DaoProcess_GetMap( self, vmc, vmc->code == DVM_HASH );

	if( bval == 2 && pp[opA]->type ==0 && pp[opA+1]->type ==0 ) return;
	for( i=0; i<bval-1; i+=2 ){
		if( (c = DaoMap_Insert( map, pp[opA+i], pp[opA+i+1] ) ) ){
			if( c ==1 ){
				DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "key not matching" );
			}else if( c ==2 ){
				DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "value not matching" );
			}
			break;
		}
	}
	if( bval >0 && self->activeTypes[ vmc->c ] ==NULL ){
		/* for constant evaluation only */
		DaoType *tp[2], *t, *any = dao_type_any;
		tp[0] = DaoNamespace_GetType( ns, pp[opA] );
		tp[1] = DaoNamespace_GetType( ns, pp[opA+1] );
		for(i=2; i<bval; i+=2){
			DaoType *tk = DaoNamespace_GetType( ns, pp[opA+i] );
			DaoType *tv = DaoNamespace_GetType( ns, pp[opA+i+1] );
			if( DaoType_MatchTo( tk, tp[0], 0 )==0 ) tp[0] = any;
			if( DaoType_MatchTo( tv, tp[1], 0 )==0 ) tp[1] = any;
			if( tp[0] ==any && tp[1] ==any ) break;
		}
		t = DaoNamespace_MakeType( ns, "map", DAO_MAP, NULL, tp, 2 );
		GC_ShiftRC( t, map->unitype );
		map->unitype = t;
	}
}
void DaoProcess_DoMatrix( DaoProcess *self, DaoVmCode *vmc )
{
#ifdef DAO_WITH_NUMARRAY
	const ushort_t opA = vmc->a;
	const ushort_t bval = vmc->b;
	daoint i, size, numtype = DAO_INTEGER;
	DaoValue **regv = self->activeValues;
	DaoArray *array = NULL;
	daoint dim[2];

	dim[0] = bval >> 8;
	dim[1] = bval & 0xff;
	size = dim[0] * dim[1];
	array = DaoProcess_GetArray( self, vmc );
	if( size ){
		numtype = regv[opA]->type;
		if( numtype == DAO_NONE || numtype > DAO_COMPLEX ){
			DaoProcess_RaiseException( self, DAO_ERROR, "invalid matrix enumeration" );
			return;
		}
	}
	if( array->etype == DAO_NONE ) array->etype = numtype;
	/* TODO: more restrict type checking on elements. */
	DaoArray_ResizeArray( array, dim, 2 );
	if( numtype == DAO_INTEGER ){
		daoint *vec = array->data.i;
		for(i=0; i<size; i++) vec[i] = DaoValue_GetInteger( regv[ opA+i ] );
	}else if( numtype == DAO_FLOAT ){
		float *vec = array->data.f;
		for(i=0; i<size; i++) vec[i] = DaoValue_GetFloat( regv[ opA+i ] );
	}else if( numtype == DAO_DOUBLE ){
		double *vec = array->data.d;
		for(i=0; i<size; i++) vec[i] = DaoValue_GetDouble( regv[ opA+i ] );
	}else{
		complex16 *vec = array->data.c;
		for(i=0; i<size; i++) vec[i] = DaoValue_GetComplex( regv[ opA+i ] );
	}
#else
	self->activeCode = vmc;
	DaoProcess_RaiseException( self, DAO_ERROR, getCtInfo( DAO_DISABLED_NUMARRAY ) );
#endif
}

DaoType* DaoRoutine_PartialCheck( DaoNamespace *NS, DaoType *T, DArray *RS, DArray *TS, int C, int *W, int *M );

void DaoProcess_DoPacking( DaoProcess *self, DaoVmCode *vmc )
{
	int i, k;
	int opa = vmc->a;
	int opb = vmc->b;
	DaoObject *object;
	DaoVariable **mtype;
	DaoValue **values = self->activeValues + opa + 1;
	DaoValue *p = self->activeValues[opa];
	DaoValue *selfobj = NULL;
	DNode *node;

	if( vmc->code == DVM_MPACK && p->type != DAO_ROUTINE ){
		selfobj = values[0];
		values ++;
		opb --;
	}

	self->activeCode = vmc;
	switch( p->type ){
	case DAO_CLASS :
		{
			DaoClass *klass = & p->xClass;
			object = DaoObject_New( klass );
			DaoProcess_SetValue( self, vmc->c, (DaoValue*)object );
			mtype = klass->instvars->items.pVar;
			if( !(klass->attribs & DAO_CLS_AUTO_DEFAULT) ){
				DaoProcess_RaiseException( self, DAO_ERROR, "cannot initialize instance" );
				break;
			}else if( opb >= object->valueCount ){
				DaoProcess_RaiseException( self, DAO_ERROR, "enumerating too many members" );
				break;
			}
			for( i=0; i<opb; i++){
				k = i+1; /* skip self */
				p = values[i];
				if( p->type == DAO_PAR_NAMED ){
					DaoNameValue *nameva = & p->xNameValue;
					node = DMap_Find( klass->lookupTable, nameva->name );
					if( node == NULL || LOOKUP_ST( node->value.pInt ) != DAO_OBJECT_VARIABLE ){
						DaoProcess_RaiseException( self, DAO_ERROR_FIELD_NOTEXIST, "" );
						break;
					}
					k = LOOKUP_ID( node->value.pInt );
					p = nameva->value;
				}
				if( DaoValue_Move( p, object->objValues + k, mtype[k]->dtype ) ==0 ){
					DaoType *type = DaoNamespace_GetType( self->activeNamespace, p );
					DaoProcess_RaiseTypeError( self, type, mtype[k]->dtype, "moving" );
					break;
				}
			}
			break;
		}
	case DAO_ROUTINE :
		{
			int wh = 0, mc = 0, call = DVM_CALL + (vmc->code - DVM_PACK);
			DaoNamespace *NS = self->activeNamespace;
			DaoRoutine *parout = DaoRoutine_New( NS, NULL, 0 );
			DaoRoutine *routine = (DaoRoutine*) p;
			DaoType *routype = routine->routType;
			DaoList *bindings = NULL;
			DArray *routines = NULL;
			DArray *partypes = DArray_New(0);

			for(i=0; i<opb; i++) DArray_Append( partypes, DaoNamespace_GetType( NS, values[i] ) );

			if( routine->overloads ){
				routines = routine->overloads->routines;
			}else if( routine->body == NULL && routine->pFunc == NULL && routine->original ){
				bindings = routine->routConsts;
				routine = routine->original;
			}
			parout->routType = DaoRoutine_PartialCheck( NS, routype, routines, partypes, call, & wh, & mc );
			GC_IncRC( parout->routType );
			DArray_Delete( partypes );
			if( mc > 1 ){
				DaoRoutine_Delete( parout );
				DaoProcess_RaiseException( self, DAO_ERROR,
						"ambigious partial function application on overloaded functions" );
				break;
			}else if( parout->routType == NULL ){
				DaoRoutine_Delete( parout );
				DaoProcess_RaiseException( self, DAO_ERROR, "invalid partial function application" );
				break;
			}
			if( routine->overloads ){
				parout->original = routines->items.pRoutine[wh];
			}else{
				parout->original = routine;
			}
			GC_IncRC( parout->original );
			if( bindings ) DArray_Assign( & parout->routConsts->items, & bindings->items );
			/* skip the self value if the routine needs none: */
			i = vmc->code == DVM_MPACK && (parout->original->routType->attrib & DAO_TYPE_SELF) == 0;
			for(; i<opb; i++) DArray_Append( & parout->routConsts->items, values[i] );
			DaoProcess_SetValue( self, vmc->c, (DaoValue*) parout );
			break;
		}
	case DAO_TYPE :
		{
			DaoType *type = (DaoType*) p;
			DaoType *retype = DaoProcess_GetCallReturnType( self, vmc, type->tid );
			complex16 c = {0.0,0.0};
			complex16 *cplx;
			DLong  *lng;
			DString *str;
			DaoArray *vec;
			DaoList *list;
			DaoTuple *tuple;
			if( retype != type && DaoType_MatchTo( type, retype, NULL ) == 0 ){
				DaoProcess_RaiseException( self, DAO_ERROR, "invalid enumeration" );
				break;
			}
			switch( type->tid ){
			case DAO_COMPLEX :
			case DAO_LONG :
			case DAO_STRING :
				for(i=0; i<opb; ++i){
					int tid = values[i]->type;
					if( tid == 0 || tid > DAO_DOUBLE ){
						DaoProcess_RaiseException( self, DAO_ERROR, "need numbers in enumeration" );
						return;
					}
				}
				break;
			}
			switch( type->tid ){
			case DAO_COMPLEX :
				cplx = DaoProcess_PutComplex( self, c );
				if( opb > 0 ) cplx->real = DaoValue_GetDouble( values[0] );
				if( opb > 1 ) cplx->imag = DaoValue_GetDouble( values[1] );
				if( opb > 2 ) DaoProcess_RaiseException( self, DAO_WARNING, "too many values" );
				break;
#ifdef DAO_WITH_LONGINT
			case DAO_LONG :
				lng = DaoProcess_PutLong( self );
				for(i=0; i<opb; ++i){
					daoint digit = DaoValue_GetInteger( values[i] );
					if( digit < 0 || digit > 255 ){
						DaoProcess_RaiseException( self, DAO_ERROR, "invalid digit" );
						return;
					}
					DLong_PushFront( lng, digit );
				}
				break;
#endif
			case DAO_STRING :
				str = DaoProcess_PutWCString( self, L"" );
				DString_Resize( str, opb );
				for(i=0; i<opb; ++i){
					daoint ch = DaoValue_GetInteger( values[i] );
					if( ch < 0 ){
						DaoProcess_RaiseException( self, DAO_ERROR, "invalid character" );
						return;
					}
					str->wcs[i] = ch;
				}
				break;
#ifdef DAO_WITH_NUMARRAY
			case DAO_ARRAY :
				vec = DaoProcess_GetArrayByType( self, vmc, type );
				DaoProcess_SetVectorValues( self, vec, values, opb );
				break;
#endif
			case DAO_LIST :
				list = DaoProcess_GetListByType( self, vmc, type );
				DArray_Resize( & list->items, opb, NULL );
				for(i=0; i<opb; ++i){
					if( DaoList_SetItem( list, values[i], i ) ){
						DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "invalid items" );
						return;
					}
				}
				break;
			case DAO_TUPLE :
				tuple = DaoProcess_GetTuple( self, type, opb, 0 );
				DaoProcess_MakeTuple( self, tuple, values, opb );
				break;
			default :
				DaoProcess_RaiseException( self, DAO_ERROR, "invalid enumeration" );
				break;
			}
			break;
		}
	default :
		DaoProcess_RaiseException( self, DAO_ERROR, "invalid enumeration" );
		break;
	}
}

/* Operator (in daoBitBoolArithOpers) validity rules,
 for operation involving DaoObject:

 A. when one of the operand is not DaoObject:
 1. all these operators are not valid, unless overloaded;

 B. when both operands are DaoObject:

 1. AND, OR, LT, LE, EQ, NE are valid, only if none operator
 in daoBitBoolArithOpers is overloaded; In this case,
 the operations will be based on pointers;

 2. AND, OR, LT, LE, EQ, NE are based on pointers, if they
 are used inside the function overloaded for the same
 operator. Example:

 class Test{
 operator == ( A : Test, B : Test ){
 return A == B; # this will be based on pointers!
 }
 }

 3. since "A>B" (or "A>=B") is compiled as "B<A" (or "B<=A"),
 when a DVM_LT or DVM_LE is executed, "operator<()"
 or "operator<=()" will be search first, if not found,
 then "operator>()" or "operator>=()" is searched,
 and applied by swapping A and B'

 4. "A<B" and "A>B" inside "operator<()" and "operator>()"
 or "A<=B" and "A>=B" inside "operator<=()" and "operator>=()"
 will be based on pointers.
 */
/* Examples of possible ways of operator overloading:
 All these overloading functions must be "static",
 namely, they do not require a class instance for being invoked:

 Unary operation:
 operator ! ( C : Number, A : Number ){... return C_or_something_else}
 operator ! ( A : Number ){... return something}

 Binary operation:
 operator + ( C : Number, A : Number, B : Number ){... return C_or_else}
 operator + ( A : Number, B : Number ){... return something}

 The first method is always tried first if C is found NOT to be null,
 and have reference count equal to one;

 For binary operation, if C == A, the following will be tried first:
 operator += ( C : Number, B : Number ){... return C_or_else}
 */
static int DaoProcess_TryUserArith( DaoProcess *self, DaoValue *A, DaoValue *B, DaoValue *C )
{
	DaoRoutine *rout = 0;
	DaoObject *object = (DaoObject*)A;
	DaoCdata *cdata = (DaoCdata*)A;
	DaoClass *klass;
	DString *name = self->mbstring;
	DaoValue **p, *par[3];
	DaoValue *value = NULL;
	int code = self->activeCode->code;
	int boolres = code >= DVM_AND && code <= DVM_NE;
	int bothobj = B ? A->type == B->type : 0;
	int recursive = 0;
	int overloaded = 0;
	int compo = 0; /* try composite operator */
	int nopac = 0; /* do not pass C as parameter */
	int npar = 3;
	int first = 1;
	int n, rc = 0;

	/* C = A + B */
	par[0] = C;
	par[1] = A;
	par[2] = B;
	if( C == A && daoBitBoolArithOpers2[ code-DVM_NOT ] ){
		DString_SetMBS( name, daoBitBoolArithOpers2[ code-DVM_NOT ] );
		if( A->type == DAO_OBJECT ){
			if( DString_EQ( name, self->activeRoutine->routName ) ) recursive = 1;
			if( recursive && object->defClass->objType == self->activeRoutine->routHost ) return 0;
			klass = object->defClass;
			overloaded = klass->attribs & DAO_OPER_OVERLOADED;
			rc = DaoObject_GetData( object, name, & value,  self->activeObject );
		}else{ /* DAO_CDATA */
			value = (DaoValue*) DaoType_FindFunction( cdata->ctype, name );
		}
		if( rc == 0 && value && value->type == DAO_ROUTINE ){
			rout = (DaoRoutine*) value;
			/* Check the method with self parameter first, then other methods: */
			if( DaoProcess_PushCallable( self, rout, A, & B, B!=NULL ) == 0 ) return 1;
			if( DaoProcess_PushCallable( self, rout, NULL, par+1, 2 ) == 0 ) return 1;
		}
	}
	DString_SetMBS( name, daoBitBoolArithOpers[ code-DVM_NOT ] );
TryAgain:
	if( A->type == DAO_OBJECT ){
		if( DString_EQ( name, self->activeRoutine->routName ) ) recursive = 1;
		if( recursive && object->defClass->objType == self->activeRoutine->routHost ) return 0;
		klass = object->defClass;
		overloaded = klass->attribs & DAO_OPER_OVERLOADED;
		rc = DaoObject_GetData( object, name, & value,  self->activeObject );
	}else{ /* DAO_CDATA */
		value = (DaoValue*) DaoType_FindFunction( cdata->ctype, name );
	}
	if( rc == 0 && value && value->type == DAO_ROUTINE ){
		rout = (DaoRoutine*) value;
		if( C && C->xBase.refCount == 1 ){ /* Check methods that can take three parameters: */
			/* Check only static method that takes parameters: C, A, B: */
			if( DaoProcess_PushCallable( self, rout, NULL, par, 2+(B!=NULL) ) == 0 ) return 1;
		}
		/* Check the method with self parameter first, then other methods: */
		if( DaoProcess_PushCallable( self, rout, A, & B, B!=NULL ) == 0 ) return 1;
		if( DaoProcess_PushCallable( self, rout, NULL, par+1, 1+(B!=NULL) ) == 0 ) return 1;
	}
	if( first && (code == DVM_LT || code == DVM_LE) ){
		first = 0;
		if( code == DVM_LT ){
			DString_SetMBS( name, ">" );
		}else{
			DString_SetMBS( name, ">=" );
		}
		if( B && (B->type == DAO_OBJECT || B->type == DAO_CDATA || B->type == DAO_CSTRUCT) ){
			par[1] = B;
			par[2] = A;
			A = par[1];
			B = par[2];
			goto TryAgain;
		}
	}
	return 0;
}
#ifdef DAO_WITH_LONGINT
static void DaoProcess_LongDiv ( DaoProcess *self, DLong *z, DLong *x, DLong *y, DLong *r )
{
	if( x->size ==0 || (x->size ==1 && x->data[0] ==0) ){
		DaoProcess_RaiseException( self, DAO_ERROR_FLOAT_DIVBYZERO, "" );
		return;
	}
	DLong_Div( z, x, y, r );
}
static int DaoProcess_CheckLong2Integer( DaoProcess *self, DLong *x )
{
	daoint d = 8*sizeof(daoint);
	if( x->size * LONG_BITS < d ) return 1;
	if( (x->size - 1) * LONG_BITS >= d ) goto RaiseInexact;
	d -= (x->size - 1) * LONG_BITS + 1; /* one bit for sign */
	if( (x->data[ x->size - 1 ] >> d) > 0 ) goto RaiseInexact;
	return 1;
RaiseInexact:
	DaoProcess_RaiseException( self, DAO_ERROR_VALUE,
							  "long integer value is too big for the operation" );
	return 0;
}
#endif
void DaoProcess_DoBinArith( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *A = self->activeValues[ vmc->a ];
	DaoValue *B = self->activeValues[ vmc->b ];
	DaoValue *C = self->activeValues[ vmc->c ];

	self->activeCode = vmc;
	if( A == NULL || B == NULL ){
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "on none object" );
		return;
	}

	if( A->type == DAO_OBJECT || A->type == DAO_CDATA || A->type == DAO_CSTRUCT ){
		self->activeCode = vmc;
		if( DaoProcess_TryUserArith( self, A, B, C ) == 0 ){
			DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "" );
		}
		return;
	}

	if( A->type >= DAO_INTEGER && A->type <= DAO_DOUBLE && B->type >= DAO_INTEGER && B->type <= DAO_DOUBLE ){
		DaoValue *val;
		DaoValue temp = {0};
		int type = A->type > B->type ? A->type : B->type;
		double va, vb, res = 0;
		memset( & temp, 0, sizeof(DaoValue) );
		switch( vmc->code ){
			case DVM_MOD:
				va = DaoValue_GetDouble( A );
				vb = DaoValue_GetDouble( B );
				if( vb ==0 ){
					DaoProcess_RaiseException( self, DAO_ERROR_FLOAT_DIVBYZERO, "" );
				}
				res = va - vb * (daoint)(va/vb);
				break;
			case DVM_ADD: res = DaoValue_GetDouble( A ) + DaoValue_GetDouble( B ); break;
			case DVM_SUB: res = DaoValue_GetDouble( A ) - DaoValue_GetDouble( B ); break;
			case DVM_MUL: res = DaoValue_GetDouble( A ) * DaoValue_GetDouble( B ); break;
			case DVM_DIV: res = DaoValue_GetDouble( A ) / DaoValue_GetDouble( B ); break;
			case DVM_POW: res = powf( DaoValue_GetDouble( A ), DaoValue_GetDouble( B ) ); break;
			default : break;
		}
		val = (DaoValue*) & temp;
		val->type = type;
		switch( type ){
			case DAO_INTEGER: val->xInteger.value = res; break;
			case DAO_FLOAT :  val->xFloat.value = res; break;
			case DAO_DOUBLE : val->xDouble.value = res; break;
			default : val->type = 0;  break;
		}
		DaoProcess_SetValue( self, vmc->c, val );
		return;
	}else if( B->type >=DAO_INTEGER && B->type <=DAO_DOUBLE && A->type ==DAO_COMPLEX ){
		DaoComplex res = {DAO_COMPLEX,0,0,0,0,{0.0,0.0}};
		double f = DaoValue_GetDouble( B );
		res.value.real = A->xComplex.value.real;
		res.value.imag = A->xComplex.value.imag;
		switch( vmc->code ){
			case DVM_ADD: res.value.real += f; break;
			case DVM_SUB: res.value.real -= f; break;
			case DVM_MUL: res.value.real *= f; res.value.imag *= f; break;
			case DVM_DIV: res.value.real /= f; res.value.imag /= f; break;
			default: break; /* XXX: pow for complex??? */
		}
		DaoProcess_SetValue( self, vmc->c, (DaoValue*) & res );
	}else if( A->type >=DAO_INTEGER && A->type <=DAO_DOUBLE && B->type ==DAO_COMPLEX ){
		DaoComplex res = {DAO_COMPLEX,0,0,0,0,{0.0,0.0}};
		double n, f = DaoValue_GetDouble( A );
		double real = B->xComplex.value.real;
		double imag = B->xComplex.value.imag;
		switch( vmc->code ){
			case DVM_DIV:
				n = real * real + imag * imag;
				res.value.real = f * real / n;
				res.value.imag = f * imag / n;
				break;
			case DVM_ADD: res.value.real = f + real;  res.value.imag = imag; break;
			case DVM_SUB: res.value.real = f - real;  res.value.imag = - imag; break;
			case DVM_MUL: res.value.real = f * real;  res.value.imag = f * imag; break;
			default: break; /* XXX: pow for complex??? */
		}
		DaoProcess_SetValue( self, vmc->c, (DaoValue*) & res );
	}else if( A->type == DAO_COMPLEX && B->type == DAO_COMPLEX ){
		DaoComplex res = {DAO_COMPLEX,0,0,0,0,{0.0,0.0}};
		double AR = A->xComplex.value.real;
		double AI = A->xComplex.value.imag;
		double BR = B->xComplex.value.real;
		double BI = B->xComplex.value.imag;
		double N = 0;
		switch( vmc->code ){
			case DVM_ADD:
				res.value.real = AR + BR;
				res.value.imag = AI + BI;
				break;
			case DVM_SUB:
				res.value.real = AR - BR;
				res.value.imag = AI - BI;
				break;
			case DVM_MUL:
				res.value.real = AR * BR - AI * BI;
				res.value.imag = AR * BI + AI * BR;
				break;
			case DVM_DIV:
				N = BR * BR + BI * BI;
				res.value.real = (AR * BR + AI * BI) / N;
				res.value.imag = (AR * BI - AI * BR) / N;
				break;
			default: break; /* XXX: pow for complex??? */
		}
		DaoProcess_SetValue( self, vmc->c, (DaoValue*) & res );
#ifdef DAO_WITH_LONGINT
	}else if( A->type == DAO_LONG && B->type == DAO_LONG ){
		DLong *b, *c;
		if( vmc->code == DVM_POW && DaoProcess_CheckLong2Integer( self, B->xLong.value ) == 0 ) return;
		b = DLong_New();
		if( vmc->c == vmc->a || vmc->c == vmc->b ){
			c = b;
		}else{
			c = DaoProcess_GetLong( self, vmc );
		}
		switch( vmc->code ){
			case DVM_ADD : DLong_Add( c, A->xLong.value, B->xLong.value ); break;
			case DVM_SUB : DLong_Sub( c, A->xLong.value, B->xLong.value ); break;
			case DVM_MUL : DLong_Mul( c, A->xLong.value, B->xLong.value ); break;
			case DVM_DIV : DaoProcess_LongDiv( self, A->xLong.value, B->xLong.value, c, b ); break;
			case DVM_MOD : DaoProcess_LongDiv( self, A->xLong.value, B->xLong.value, b, c ); break;
			case DVM_POW : DLong_Pow( c, A->xLong.value, DLong_ToInteger( B->xLong.value ) ); break;
			default : break;
		}
		if( vmc->c == vmc->a || vmc->c == vmc->b ){
			c = DaoProcess_GetLong( self, vmc );
			DLong_Move( c, b );
		}
		DLong_Delete( b );
	}else if( A->type == DAO_LONG && B->type >= DAO_INTEGER && B->type <= DAO_DOUBLE ){
		DLong *c = vmc->a == vmc->c ? C->xLong.value : DaoProcess_GetLong( self, vmc );
		DLong *b = DLong_New();
		DLong *b2 = DLong_New();
		DLong_FromValue( b, B );
		switch( vmc->code ){
			case DVM_ADD : DLong_Add( c, A->xLong.value, b ); break;
			case DVM_SUB : DLong_Sub( c, A->xLong.value, b ); break;
			case DVM_MUL : DLong_Mul( c, A->xLong.value, b ); break;
			case DVM_DIV : DaoProcess_LongDiv( self, A->xLong.value, b, c, b2 ); break;
			case DVM_MOD : DaoProcess_LongDiv( self, A->xLong.value, b, b2, c ); break;
			case DVM_POW : DLong_Pow( c, A->xLong.value, DaoValue_GetInteger( B ) ); break;
			default: break;
		}
		DLong_Delete( b );
		DLong_Delete( b2 );
	}else if( B->type == DAO_LONG && A->type >= DAO_INTEGER && A->type <= DAO_DOUBLE ){
		DLong *a, *b2, *c = DaoProcess_GetLong( self, vmc );
		if( vmc->code == DVM_POW && DaoProcess_CheckLong2Integer( self, B->xLong.value ) == 0 ) return;
		a = DLong_New();
		b2 = DLong_New();
		DLong_FromValue( a, A );
		switch( vmc->code ){
			case DVM_ADD : DLong_Add( c, a, B->xLong.value ); break;
			case DVM_SUB : DLong_Sub( c, a, B->xLong.value ); break;
			case DVM_MUL : DLong_Mul( c, B->xLong.value, a ); break;
			case DVM_DIV : DaoProcess_LongDiv( self, a, B->xLong.value, c, b2 ); break;
			case DVM_MOD : DaoProcess_LongDiv( self, a, B->xLong.value, b2, c ); break;
			case DVM_POW : DLong_Pow( c, a, DLong_ToInteger( B->xLong.value ) ); break;
			default: break;
		}
		DLong_Delete( a );
		DLong_Delete( b2 );
#endif
#ifdef DAO_WITH_NUMARRAY
	}else if( B->type >=DAO_INTEGER && B->type <=DAO_COMPLEX && A->type ==DAO_ARRAY ){
		DaoArray *na = & A->xArray;
		DaoArray *nc = na;
		if( vmc->a != vmc->c ){
			nc = DaoProcess_GetArray( self, vmc );
			if( nc->etype == DAO_NONE ) nc->etype = na->etype;
		}
		DaoArray_array_op_number( nc, na, B, vmc->code, self );
	}else if( A->type >=DAO_INTEGER && A->type <=DAO_COMPLEX && B->type ==DAO_ARRAY ){
		DaoArray *nb = & B->xArray;
		DaoArray *nc = nb;
		if( vmc->b != vmc->c ){
			nc = DaoProcess_GetArray( self, vmc );
			if( nc->etype == DAO_NONE ) nc->etype = nb->etype;
		}
		DaoArray_number_op_array( nc, A, nb, vmc->code, self );
	}else if( A->type ==DAO_ARRAY && B->type ==DAO_ARRAY ){
		DaoArray *na = & A->xArray;
		DaoArray *nb = & B->xArray;
		DaoArray *nc;
		if( vmc->a == vmc->c ){
			nc = na;
		}else if( vmc->b == vmc->c ){
			nc = nb;
		}else{
			nc = DaoProcess_GetArray( self, vmc );
			if( nc->etype == DAO_NONE ) nc->etype = na->etype > nb->etype ? na->etype : nb->etype;
		}
		DaoArray_ArrayArith( nc, na, nb, vmc->code, self );
#endif
	}else if( A->type ==DAO_STRING && B->type ==DAO_INTEGER && vmc->code ==DVM_ADD
			 && vmc->a == vmc->c ){
		DString_AppendWChar( A->xString.data, (wchar_t) B->xInteger.value );
	}else if( A->type ==DAO_STRING && B->type ==DAO_STRING && vmc->code ==DVM_ADD ){
		if( vmc->a == vmc->c ){
			DString_Append( A->xString.data, B->xString.data );
		}else if( vmc->b == vmc->c ){
			DString_Insert( B->xString.data, A->xString.data, 0, 0, 0 );
		}else{
			DaoValue *C = DaoProcess_PutValue( self, A );
			DString_Append( C->xString.data, B->xString.data );
		}
	}else if( A->type == DAO_ENUM && B->type == DAO_ENUM
			 && (vmc->code == DVM_ADD || vmc->code == DVM_SUB) ){
		DaoType *ta = A->xEnum.etype;
		DaoType *tb = B->xEnum.etype;
		DaoEnum *denum = & A->xEnum;
		int rc = 0;
		if( vmc->c != vmc->a && ta->name->mbs[0] == '$' && tb->name->mbs[0] == '$' ){
			DaoNamespace *ns = self->activeNamespace;
			DaoType *type = NULL;
			int value = 0;
			denum = DaoProcess_GetEnum( self, vmc );
			if( vmc->code == DVM_ADD ){
				type = DaoNamespace_SymbolTypeAdd( ns, ta, tb, &value );
			}else{
				type = DaoNamespace_SymbolTypeAdd( ns, ta, tb, &value );
			}
			if( type == NULL ) DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "symbol not found in the enum" );
			DaoEnum_SetType( denum, type );
			denum->value = value;
			return;
		}
		if( vmc->c != vmc->a ){
			denum = DaoProcess_GetEnum( self, vmc );
			if( denum->etype == NULL ) DaoEnum_SetType( denum, A->xEnum.etype );
			DaoEnum_SetValue( denum, & A->xEnum, NULL );
		}
		if( vmc->code == DVM_ADD ){
			rc = DaoEnum_AddValue( denum, & B->xEnum, NULL );
		}else{
			rc = DaoEnum_RemoveValue( denum, & B->xEnum, NULL );
		}
		if( rc == 0 ){
			if( denum->etype->flagtype ==0 )
				DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "not combinable enum" );
			else
				DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "symbol not found in the enum" );
			return;
		}
	}else if( A->type == DAO_LIST && B->type == DAO_LIST && vmc->code == DVM_ADD ){
		DaoList *lA = & A->xList;
		DaoList *lB = & B->xList;
		DaoList *list;
		daoint i = 0, NA = lA->items.size, NB = lB->items.size;
		if( vmc->a == vmc->c ){
			list = lA;
			for(i=0; i<NB; i++) DaoList_Append( list, lB->items.items.pValue[i] );
		}else if( vmc->b == vmc->c ){
			list = lB;
			for(i=NA; i>0; i--) DaoList_PushFront( list, lA->items.items.pValue[i-1] );
		}else{
			list = DaoProcess_GetList( self, vmc );
			DArray_Resize( & list->items, NA + NB, NULL );
			for(i=0; i<NA; i++) DaoList_SetItem( list, lA->items.items.pValue[i], i );
			for(i=0; i<NB; i++) DaoList_SetItem( list, lB->items.items.pValue[i], i + NA );
		}
	}else if( A->type == DAO_MAP && B->type == DAO_MAP && vmc->code == DVM_ADD ){
		DaoMap *hA = & A->xMap;
		DaoMap *hB = & B->xMap;
		DaoMap *hC;
		DNode *node;
		if( vmc->a == vmc->c ){
			hC = hA;
		}else if( vmc->a == vmc->c ){
			hC = hB;
			hB = hA;
		}else{
			hC = DaoProcess_GetMap( self, vmc, hA->items->hashing );
			node = DMap_First( hA->items );
			for( ; node !=NULL; node=DMap_Next( hA->items, node) )
				DMap_Insert( hC->items, node->key.pVoid, node->value.pVoid );
		}
		node = DMap_First( hB->items );
		for( ; node !=NULL; node=DMap_Next( hB->items, node) )
			DMap_Insert( hC->items, node->key.pVoid, node->value.pVoid );
	}else{
		DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "" );
	}
}
/* binary operation with boolean result. */
void DaoProcess_DoBinBool(  DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *A = self->activeValues[ vmc->a ];
	DaoValue *B = self->activeValues[ vmc->b ];
	DaoValue *C = NULL;
	int D = 0, rc = 0;

	self->activeCode = vmc;
	if( A == NULL ) A = dao_none_value;
	if( B == NULL ) B = dao_none_value;
	if( A->type ==0 || B->type ==0 ){
		switch( vmc->code ){
			case DVM_AND: C = A->type ? B : A; break;
			case DVM_OR:  C = A->type ? A : B; break;
			case DVM_LT:  D = A->type < B->type; break;
			case DVM_LE:  D = A->type <= B->type; break;
			case DVM_EQ:  D = A->type == B->type; break;
			case DVM_NE:  D = A->type != B->type; break;
			default: break;
		}
		if( A->type == DAO_CSTRUCT || B->type == DAO_CSTRUCT ){
			D = vmc->code == DVM_NE;
		}else if( A->type == DAO_CDATA || B->type == DAO_CDATA ){
			DaoCdata *cdata = (DaoCdata*)( A->type == DAO_CDATA ? & A->xCdata : & B->xCdata );
			if( vmc->code == DVM_EQ ){
				D = cdata->data ? 0 : 1;
			}else if( vmc->code == DVM_NE ){
				D = cdata->data ? 1 : 0;
			}
		}
		if( C ) DaoProcess_PutValue( self, C );
		else DaoProcess_PutInteger( self, D );
		return;
	}

	if( A->type == DAO_OBJECT || A->type == DAO_CSTRUCT || A->type == DAO_CDATA ){
		rc = DaoProcess_TryUserArith( self, A, B, C );
		if( rc == 0 && (A->type == DAO_OBJECT || A->type == DAO_CSTRUCT) ){
			switch( vmc->code ){
			case DVM_AND: C = A ? B : A; break;
			case DVM_OR:  C = A ? A : B; break;
			case DVM_LT:  D = A < B; break;
			case DVM_LE:  D = A <= B; break;
			case DVM_EQ:  D = A == B; break;
			case DVM_NE:  D = A != B; break;
			default: break;
			}
			if( C ) DaoProcess_PutValue( self, C );
			else DaoProcess_PutInteger( self, D );
		}else if( rc == 0 ){  /* A->type == DAO_CDATA */
			if( B->type != DAO_CDATA ){
				switch( vmc->code ){
				case DVM_AND: C = A->xCdata.data ? B : A; break;
				case DVM_OR : C = A->xCdata.data ? A : B; break;
				default : D = vmc->code == DVM_NE; break;
				}
			}else{
				switch( vmc->code ){
				case DVM_AND: C = A->xCdata.data ? B : A; break;
				case DVM_OR:  C = A->xCdata.data ? A : B; break;
				case DVM_LT:  D = A->xCdata.data < B->xCdata.data; break;
				case DVM_LE:  D = A->xCdata.data <= B->xCdata.data; break;
				case DVM_EQ:  D = A->xCdata.data == B->xCdata.data; break;
				case DVM_NE:  D = A->xCdata.data != B->xCdata.data; break;
				default: break;
				}
			}
			if( C ) DaoProcess_PutValue( self, C );
			else DaoProcess_PutInteger( self, D );
		}
		return;
	}

	if( A->type >= DAO_INTEGER && A->type <= DAO_DOUBLE
	   && B->type >= DAO_INTEGER && B->type <= DAO_DOUBLE ){
		switch( vmc->code ){
			case DVM_AND: C = DaoValue_GetDouble( A ) ? B : A; break;
			case DVM_OR:  C = DaoValue_GetDouble( A ) ? A : B; break;
			case DVM_LT:  D = DaoValue_GetDouble( A ) < DaoValue_GetDouble( B ); break;
			case DVM_LE:  D = DaoValue_GetDouble( A ) <= DaoValue_GetDouble( B ); break;
			case DVM_EQ:  D = DaoValue_GetDouble( A ) == DaoValue_GetDouble( B ); break;
			case DVM_NE:  D = DaoValue_GetDouble( A ) != DaoValue_GetDouble( B ); break;
			default: break;
		}
	}else if( A->type == DAO_COMPLEX && B->type == DAO_COMPLEX ){
		double AR = A->xComplex.value.real, AI = A->xComplex.value.imag;
		double BR = B->xComplex.value.real, BI = B->xComplex.value.imag;
		switch( vmc->code ){
			case DVM_EQ: D = (AR == BR) && (AI == BI); break;
			case DVM_NE: D = (AR != BR) || (AI != BI); break;
			default: break;
		}
#ifdef DAO_WITH_LONGINT
	}else if( A->type == DAO_LONG && B->type == DAO_LONG ){
		switch( vmc->code ){
			case DVM_AND: C = DLong_CompareToZero( A->xLong.value ) ? B : A; break;
			case DVM_OR:  C = DLong_CompareToZero( A->xLong.value ) ? A : B; break;
			case DVM_LT:  D = DLong_Compare( A->xLong.value, B->xLong.value )< 0; break;
			case DVM_LE:  D = DLong_Compare( A->xLong.value, B->xLong.value )<=0; break;
			case DVM_EQ:  D = DLong_Compare( A->xLong.value, B->xLong.value )==0; break;
			case DVM_NE:  D = DLong_Compare( A->xLong.value, B->xLong.value )!=0; break;
			default: break;
		}
	}else if( A->type == DAO_INTEGER && B->type == DAO_LONG ){
		switch( vmc->code ){
			case DVM_AND: C = A->xInteger.value ? B : A; break;
			case DVM_OR:  C = A->xInteger.value ? A : B; break;
			case DVM_LT:  D = DLong_CompareToInteger( B->xLong.value, A->xInteger.value )> 0; break;
			case DVM_LE:  D = DLong_CompareToInteger( B->xLong.value, A->xInteger.value )>=0; break;
			case DVM_EQ:  D = DLong_CompareToInteger( B->xLong.value, A->xInteger.value )==0; break;
			case DVM_NE:  D = DLong_CompareToInteger( B->xLong.value, A->xInteger.value )!=0; break;
			default: break;
		}
	}else if( A->type == DAO_LONG && B->type == DAO_INTEGER ){
		switch( vmc->code ){
			case DVM_AND: C = DLong_CompareToZero( A->xLong.value ) ? B : A; break;
			case DVM_OR:  C = DLong_CompareToZero( A->xLong.value ) ? A : B; break;
			case DVM_LT:  D = DLong_CompareToInteger( A->xLong.value, B->xInteger.value )< 0; break;
			case DVM_LE:  D = DLong_CompareToInteger( A->xLong.value, B->xInteger.value )<=0; break;
			case DVM_EQ:  D = DLong_CompareToInteger( A->xLong.value, B->xInteger.value )==0; break;
			case DVM_NE:  D = DLong_CompareToInteger( A->xLong.value, B->xInteger.value )!=0; break;
			default: break;
		}
	}else if( (A->type == DAO_FLOAT || A->type == DAO_DOUBLE) && B->type == DAO_LONG ){
		double va = DaoValue_GetDouble( A );
		switch( vmc->code ){
			case DVM_AND: C = va ? B : A; break;
			case DVM_OR:  C = va ? A : B; break;
			case DVM_LT:  D = DLong_CompareToDouble( B->xLong.value, va )> 0; break;
			case DVM_LE:  D = DLong_CompareToDouble( B->xLong.value, va )>=0; break;
			case DVM_EQ:  D = DLong_CompareToDouble( B->xLong.value, va )==0; break;
			case DVM_NE:  D = DLong_CompareToDouble( B->xLong.value, va )!=0; break;
			default: break;
		}
	}else if( A->type == DAO_LONG && (B->type == DAO_FLOAT || B->type == DAO_DOUBLE) ){
		double vb = DaoValue_GetDouble( B );
		switch( vmc->code ){
			case DVM_AND: C = DLong_CompareToZero( A->xLong.value ) ? B : A; break;
			case DVM_OR:  C = DLong_CompareToZero( A->xLong.value ) ? A : B; break;
			case DVM_LT:  D = DLong_CompareToDouble( A->xLong.value, vb )< 0; break;
			case DVM_LE:  D = DLong_CompareToDouble( A->xLong.value, vb )<=0; break;
			case DVM_EQ:  D = DLong_CompareToDouble( A->xLong.value, vb )==0; break;
			case DVM_NE:  D = DLong_CompareToDouble( A->xLong.value, vb )!=0; break;
			default: break;
		}
#endif
	}else if( A->type == DAO_STRING && B->type == DAO_STRING ){
		switch( vmc->code ){
			case DVM_AND: C = DString_Size( A->xString.data ) ? B : A; break;
			case DVM_OR:  C = DString_Size( A->xString.data ) ? A : B; break;
			case DVM_LT:  D = DString_Compare( A->xString.data, B->xString.data )<0; break;
			case DVM_LE:  D = DString_Compare( A->xString.data, B->xString.data )<=0; break;
			case DVM_EQ:  D = DString_Compare( A->xString.data, B->xString.data )==0; break;
			case DVM_NE:  D = DString_Compare( A->xString.data, B->xString.data )!=0; break;
			default: break;
		}
	}else if( (A->type == DAO_ENUM && B->type == DAO_ENUM)
			 || (A->type == DAO_TUPLE && B->type == DAO_TUPLE) ){
		switch( vmc->code ){
			case DVM_AND: C = DaoValue_GetInteger( A ) ? B : A; break;
			case DVM_OR:  C = DaoValue_GetInteger( A ) ? A : B; break;
			case DVM_LT:  D = DaoValue_Compare( A, B ) < 0; break;
			case DVM_LE:  D = DaoValue_Compare( A, B ) <= 0; break;
			case DVM_EQ:  D = DaoValue_Compare( A, B ) == 0; break;
			case DVM_NE:  D = DaoValue_Compare( A, B ) != 0; break;
			default: break;
		}
	}else if( vmc->code == DVM_AND || vmc->code == DVM_OR ){
		DaoValue *AA = A, *BB = B;
		/* TODO: trict type checking! */
		if( vmc->code == DVM_OR ){ AA = B; BB = A; }
		switch( A->type ){
			case DAO_INTEGER : C = A->xInteger.value ? BB : AA; break;
			case DAO_FLOAT   : C = A->xFloat.value ? BB : AA; break;
			case DAO_DOUBLE  : C = A->xDouble.value ? BB : AA; break;
			case DAO_COMPLEX : C = A->xComplex.value.real && A->xComplex.value.imag ? BB : AA; break;
#ifdef DAO_WITH_LONGINT
			case DAO_LONG : C = DLong_CompareToZero( A->xLong.value ) ? BB : AA; break;
#endif
			case DAO_STRING : C = DString_Size( A->xString.data ) ? BB : AA; break;
			case DAO_ENUM : C = A->xEnum.value ? BB : AA; break;
			case DAO_LIST : C = A->xList.items.size ? BB : AA; break;
			case DAO_MAP  : C = A->xMap.items->size ? BB : AA; break;
			case DAO_ARRAY : C = A->xArray.size ? BB : AA; break;
			default : break;
		}
		if( C == NULL ){
			DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "" );
			return;
		}
	}else if( A->type == B->type && (A->type == DAO_LIST || A->type == DAO_ARRAY) ){
		D = DaoValue_Compare( A, B );
		switch( vmc->code ){
		case DVM_LT:
			if( abs( D ) > 1 ) goto InvalidOperation;
			D = D <  0;
			break;
		case DVM_LE:
			if( abs( D ) > 1 ) goto InvalidOperation;
			D = D <= 0;
			break;
		case DVM_EQ: D = D == 0; break;
		case DVM_NE: D = D != 0; break;
		default: break;
		}
	}else if( vmc->code == DVM_EQ ){
		D = A == B;
	}else if( vmc->code == DVM_NE ){
		D = A != B;
	}else{
InvalidOperation:
		DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "" );
		return;
	}
	if( C ) DaoProcess_PutValue( self, C );
	else DaoProcess_PutInteger( self, D );
}
void DaoProcess_DoUnaArith( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *A = self->activeValues[ vmc->a ];
	DaoValue *C = NULL;
	int ta = A->type;
	self->activeCode = vmc;
	if( A->type ==0 ){
		DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "on none object" );
		return;
	}

	if( ta == DAO_INTEGER ){
		C = DaoProcess_SetValue( self, vmc->c, A );
		switch( vmc->code ){
			case DVM_NOT :  C->xInteger.value = ! C->xInteger.value; break;
			case DVM_MINUS : C->xInteger.value = - C->xInteger.value; break;
			default: break;
		}
	}else if( ta == DAO_FLOAT ){
		C = DaoProcess_SetValue( self, vmc->c, A );
		switch( vmc->code ){
			case DVM_NOT :  C->xInteger.value = ! C->xFloat.value; break;
			case DVM_MINUS : C->xFloat.value = - C->xFloat.value; break;
			default: break;
		}
	}else if( ta == DAO_DOUBLE ){
		C = DaoProcess_SetValue( self, vmc->c, A );
		switch( vmc->code ){
			case DVM_NOT :  C->xInteger.value = ! C->xDouble.value; break;
			case DVM_MINUS : C->xDouble.value = - C->xDouble.value; break;
			default: break;
		}
	}else if( ta == DAO_COMPLEX ){
		if( vmc->code == DVM_MINUS ){
			C = DaoProcess_SetValue( self, vmc->c, A );
			C->xComplex.value.real = - C->xComplex.value.real;
			C->xComplex.value.imag = - C->xComplex.value.imag;
		}
#ifdef DAO_WITH_LONGINT
	}else if( ta == DAO_LONG ){
		C = DaoProcess_SetValue( self, vmc->c, A );
		switch( vmc->code ){
			case DVM_NOT  :
				ta = DLong_CompareToZero( C->xLong.value ) == 0;
				DLong_FromInteger( C->xLong.value, ta );
				break;
			case DVM_MINUS : C->xLong.value->sign = - C->xLong.value->sign; break;
			default: break;
		}
#endif
#ifdef DAO_WITH_NUMARRAY
	}else if( ta == DAO_ENUM ){
		DaoProcess_PutInteger( self, ! A->xEnum.value );
		return;
	}else if( ta == DAO_ARRAY ){
		DaoArray *array = & A->xArray;
		daoint i, n;
		C = A;
		if( array->etype == DAO_FLOAT ){
			DaoArray *res = DaoProcess_GetArray( self, vmc );
			DaoArray_SetNumType( res, array->etype );
			DaoArray_ResizeArray( res, array->dims, array->ndim );
			if( array->etype == DAO_FLOAT ){
				float *va = array->data.f;
				float *vc = res->data.f;
				if( vmc->code == DVM_NOT ){
					for(i=0,n=array->size; i<n; i++ ) vc[i] = (float) ! va[i];
				}else{
					for(i=0,n=array->size; i<n; i++ ) vc[i] = - va[i];
				}
			}else{
				double *va = array->data.d;
				double *vc = res->data.d;
				if( vmc->code == DVM_NOT ){
					for(i=0,n=array->size; i<n; i++ ) vc[i] = ! va[i];
				}else{
					for(i=0,n=array->size; i<n; i++ ) vc[i] = - va[i];
				}
			}
		}else if( vmc->code == DVM_MINUS ){
			DaoArray *res = DaoProcess_GetArray( self, vmc );
			complex16 *va, *vc;
			DaoArray_SetNumType( res, array->etype );
			DaoArray_ResizeArray( res, array->dims, array->ndim );
			va = array->data.c;
			vc = res->data.c;
			for(i=0,n=array->size; i<n; i++ ){
				vc[i].real = - va[i].real;
				vc[i].imag = - va[i].imag;
			}
		}
#endif
	}else if( ta == DAO_OBJECT || ta == DAO_CDATA || ta == DAO_CSTRUCT ){
		C = self->activeValues[ vmc->c ];
		if( DaoProcess_TryUserArith( self, A, NULL, C ) == 0 ){
			if( vmc->code == DVM_NOT ){
				if( ta == DAO_OBJECT ){
					DaoValue *deft = A->xObject.defClass->objType->value;
					DaoProcess_PutInteger( self, A == deft );
				}else{
					DaoProcess_PutInteger( self, A->xCdata.data == NULL );
				}
				return;
			}
			DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "" );
		}
		return;
	}
	if( C == NULL ) DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "" );
}
void DaoProcess_DoInTest( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *A = self->activeValues[ vmc->a ];
	DaoValue *B = self->activeValues[ vmc->b ];
	daoint *C = DaoProcess_PutInteger( self, 0 );
	daoint i, n;
	if( A->type == DAO_INTEGER && B->type == DAO_STRING ){
		daoint bv = A->xInteger.value;
		daoint size = B->xString.data->size;
		if( B->xString.data->mbs ){
			char *mbs = B->xString.data->mbs;
			for(i=0; i<size; i++){
				if( mbs[i] == bv ){
					*C = 1;
					break;
				}
			}
		}else{
			wchar_t *wcs = B->xString.data->wcs;
			for(i=0; i<size; i++){
				if( wcs[i] == bv ){
					*C = 1;
					break;
				}
			}
		}
	}else if( A->type == DAO_STRING && B->type == DAO_STRING ){
		*C = DString_Find( B->xString.data, A->xString.data, 0 ) != MAXSIZE;
	}else if( A->type == DAO_ENUM && B->type == DAO_ENUM ){
		DaoType *ta = A->xEnum.etype;
		DaoType *tb = B->xEnum.etype;
		if( ta == tb ){
			*C = A->xEnum.value == (A->xEnum.value & B->xEnum.value);
		}else{
			DMap *ma = ta->mapNames;
			DMap *mb = tb->mapNames;
			DNode *it, *node;
			*C = 1;
			for(it=DMap_First(ma); it; it=DMap_Next(ma,it) ){
				if( ta->flagtype ){
					if( !(it->value.pInt & A->xEnum.value) ) continue;
				}else if( it->value.pInt != A->xEnum.value ){
					continue;
				}
				if( (node = DMap_Find( mb, it->key.pVoid )) == NULL ){
					*C = 0;
					break;
				}
				if( !(node->value.pInt & B->xEnum.value) ){
					*C = 0;
					break;
				}
			}
		}
	}else if( B->type == DAO_LIST ){
		DArray *items = & B->xList.items;
		DaoType *ta = DaoNamespace_GetType( self->activeNamespace, A );
		if( ta && B->xList.unitype && B->xList.unitype->nested->size ){
			DaoType *tb = B->xList.unitype->nested->items.pType[0];
			if( tb && DaoType_MatchTo( ta, tb, NULL ) == 0 ) return;
		}
		for(i=0,n=items->size; i<n; i++){
			*C = DaoValue_Compare( A, items->items.pValue[i] ) ==0;
			if( *C ) break;
		}
	}else if( B->type == DAO_MAP ){
		DaoType *ta = DaoNamespace_GetType( self->activeNamespace, A );
		if( ta && B->xMap.unitype && B->xMap.unitype->nested->size ){
			DaoType *tb = B->xMap.unitype->nested->items.pType[0];
			if( tb && DaoType_MatchTo( ta, tb, NULL ) < DAO_MT_SUB	 ) return;
		}
		*C = DMap_Find( B->xMap.items, A ) != NULL;
	}else if( B->type == DAO_TUPLE && B->xTuple.subtype == DAO_PAIR ){
		int c1 = DaoValue_Compare( B->xTuple.items[0], A );
		int c2 = DaoValue_Compare( A, B->xTuple.items[1] );
		*C = c1 <=0 && c2 <= 0;
	}else if( B->type == DAO_TUPLE ){
		for(i=0; i<B->xTuple.size; ++i){
			if( DaoValue_Compare( A, B->xTuple.items[i] ) == 0 ){
				*C = 1;
				break;
			}
		}
	}else{
		DaoProcess_RaiseException( self, DAO_ERROR_TYPE, "" );
	}
}
void DaoProcess_DoBitLogic( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *A = self->activeValues[ vmc->a ];
	DaoValue *B = self->activeValues[ vmc->b ];
	size_t inum = 0;

	self->activeCode = vmc;
	if( A->type && B->type && A->type <= DAO_DOUBLE && B->type <= DAO_DOUBLE ){
		switch( vmc->code ){
			case DVM_BITAND: inum =DaoValue_GetInteger(A) & DaoValue_GetInteger(B);break;
			case DVM_BITOR: inum =DaoValue_GetInteger(A) | DaoValue_GetInteger(B);break;
			case DVM_BITXOR: inum =DaoValue_GetInteger(A) ^ DaoValue_GetInteger(B);break;
			default : break;
		}
		if( A->type == DAO_DOUBLE || B->type == DAO_DOUBLE ){
			DaoProcess_PutDouble( self, inum );
		}else if( A->type == DAO_FLOAT || B->type == DAO_FLOAT ){
			DaoProcess_PutFloat( self, inum );
		}else{
			DaoProcess_PutInteger( self, inum );
		}
#ifdef DAO_WITH_LONGINT
	}else if( A->type == DAO_LONG && B->type >= DAO_INTEGER && B->type <= DAO_DOUBLE ){
		DLong *bigint = DaoProcess_PutLong( self );
		DLong_FromValue( bigint, B );
		switch( vmc->code ){
			case DVM_BITAND : DLong_BitAND( bigint, A->xLong.value, bigint ); break;
			case DVM_BITOR :  DLong_BitOR( bigint, A->xLong.value, bigint ); break;
			case DVM_BITXOR : DLong_BitXOR( bigint, A->xLong.value, bigint ); break;
			default : break;
		}
	}else if( B->type == DAO_LONG && A->type >= DAO_INTEGER && A->type <= DAO_DOUBLE ){
		DLong *bigint = DaoProcess_PutLong( self );
		DLong_FromValue( bigint, A );
		switch( vmc->code ){
			case DVM_BITAND : DLong_BitAND( bigint, B->xLong.value, bigint ); break;
			case DVM_BITOR :  DLong_BitOR( bigint, B->xLong.value, bigint ); break;
			case DVM_BITXOR : DLong_BitXOR( bigint, B->xLong.value, bigint ); break;
			default : break;
		}
	}else if( A->type == DAO_LONG && B->type == DAO_LONG ){
		DLong *bigint = DaoProcess_PutLong( self );
		switch( vmc->code ){
			case DVM_BITAND : DLong_BitAND( bigint, A->xLong.value, B->xLong.value ); break;
			case DVM_BITOR :  DLong_BitOR( bigint, A->xLong.value, B->xLong.value ); break;
			case DVM_BITXOR : DLong_BitXOR( bigint, A->xLong.value, B->xLong.value ); break;
			default : break;
		}
#endif
	}else if( A->type == DAO_ENUM && B->type == DAO_ENUM ){
		DaoEnum *en = DaoProcess_GetEnum( self, vmc );
		if( A->xEnum.etype != B->xEnum.etype ) goto InvalidOperation;
		if( en == NULL || en->etype != A->xEnum.etype ) goto InvalidOperation;
		switch( vmc->code ){
		case DVM_BITAND : en->value = A->xEnum.value & B->xEnum.value; break;
		case DVM_BITOR  : en->value = A->xEnum.value | B->xEnum.value; break;
		default : goto InvalidOperation;
		}
	}else{
InvalidOperation:
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "invalid operands" );
	}
}
void DaoProcess_DoBitShift( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *A = self->activeValues[ vmc->a ];
	DaoValue *B = self->activeValues[ vmc->b ];
	if( A->type && B->type && A->type <= DAO_DOUBLE && B->type <= DAO_DOUBLE ){
		daoint inum = 0;
		if( vmc->code == DVM_BITLFT ){
			inum = DaoValue_GetInteger(A) << DaoValue_GetInteger(B);
		}else{
			inum = DaoValue_GetInteger(A) >> DaoValue_GetInteger(B);
		}
		if( A->type == DAO_DOUBLE || B->type == DAO_DOUBLE ){
			DaoProcess_PutDouble( self, inum );
		}else if( A->type == DAO_FLOAT || B->type == DAO_FLOAT ){
			DaoProcess_PutFloat( self, inum );
		}else{
			DaoProcess_PutInteger( self, inum );
		}
#ifdef DAO_WITH_LONGINT
	}else if( A->type ==DAO_LONG && B->type >=DAO_INTEGER && B->type <= DAO_DOUBLE ){
		if( vmc->a == vmc->c ){
			if( vmc->code == DVM_BITLFT ){
				DLong_ShiftLeft( A->xLong.value, DaoValue_GetInteger( B ) );
			}else{
				DLong_ShiftRight( A->xLong.value, DaoValue_GetInteger( B ) );
			}
		}else{
			DLong *bigint = DaoProcess_PutLong( self );
			DLong_Move( bigint, A->xLong.value );
			if( vmc->code == DVM_BITLFT ){
				DLong_ShiftLeft( bigint, DaoValue_GetInteger( B ) );
			}else{
				DLong_ShiftRight( bigint, DaoValue_GetInteger( B ) );
			}
		}
#endif
	}else if( A->type ==DAO_LIST && (vmc->code ==DVM_BITLFT || vmc->code ==DVM_BITRIT) ){
		DaoList *list = & self->activeValues[ vmc->a ]->xList;
		self->activeCode = vmc;
		if( DaoProcess_SetValue( self, vmc->c, A ) ==0 ) return;
		if( vmc->code ==DVM_BITLFT ){
			DaoType *abtp = list->unitype;
			if( abtp && abtp->nested->size ){
				abtp = abtp->nested->items.pType[0];
				if( DaoType_MatchValue( abtp, B, NULL ) ==0 ) return; /* XXX information */
			}
			DaoList_PushBack( list, B );
		}else{
			if( list->items.size ==0 ) return; /* XXX information */
			B = list->items.items.pValue[list->items.size-1];
			if( DaoProcess_SetValue( self, vmc->b, B ) ==0 ) return;
			DArray_PopBack( & list->items );
		}
	}else{
		self->activeCode = vmc;
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "invalid operands" );
	}
}
void DaoProcess_DoBitFlip( DaoProcess *self, DaoVmCode *vmc )
{
	DaoValue *A = self->activeValues[ vmc->a ];
	self->activeCode = vmc;
	if( A->type >= DAO_INTEGER && A->type <= DAO_DOUBLE ){
		switch( A->type ){
			case DAO_INTEGER : DaoProcess_PutInteger( self, ~A->xInteger.value ); break;
			case DAO_FLOAT   : DaoProcess_PutFloat( self, ~(daoint)A->xFloat.value ); break;
			case DAO_DOUBLE  : DaoProcess_PutDouble( self, ~(daoint)A->xDouble.value ); break;
		}
	}else if( A->type == DAO_COMPLEX ){
		complex16 *C = DaoProcess_PutComplex( self, A->xComplex.value );
		C->imag = - C->imag;
#ifdef DAO_WITH_LONGINT
	}else if( A->type == DAO_LONG ){
		DLong *bigint = DaoProcess_PutLong( self );
		DLong_Move( bigint, A->xLong.value );
		DLong_Flip( bigint );
#endif
	}else if( A->type == DAO_ENUM ){
		DaoType *etype = A->xEnum.etype;
		DaoValue *C = DaoProcess_PutValue( self, A );
		DNode *it = DMap_First(etype->mapNames);
		int min = 0, max = 0, value = 0;
		if( it ) min = max = it->value.pInt;
		for(; it; it=DMap_Next(etype->mapNames,it)){
			if( it->value.pInt < min ) min = it->value.pInt;
			if( it->value.pInt > max ) max = it->value.pInt;
			value |= it->value.pInt;
		}
		if( etype->flagtype ){
			C->xEnum.value = value & (~A->xEnum.value);
		}else if( A->xEnum.value == min ){
			C->xEnum.value = max;
		}else{
			C->xEnum.value = min;
		}
	}else{
		DaoProcess_RaiseException( self, DAO_ERROR_VALUE, "invalid operands" );
	}
}
#ifdef DAO_WITH_NUMARRAY
static void DaoArray_ToString( DaoArray *self, DString *str, daoint offset, daoint size )
{
	daoint i;
	int type = 1; /*MBS*/
	DString_ToWCS( str );
	DString_Resize( str, size * ( (self->etype == DAO_COMPLEX) +1 ) );
	if( self->etype == DAO_COMPLEX ){
		for(i=0; i<size; i++){
			str->wcs[2*i] = self->data.c[offset+i].real;
			str->wcs[2*i+1] = self->data.c[offset+i].imag;
			if( str->wcs[2*i] > 255 ) type = 0;
		}
	}else{
		for(i=0; i<size; i++){
			str->wcs[i] = DaoArray_GetInteger( self, offset+i );
			if( str->wcs[i] > 255 ) type = 0;
		}
	}
	if( type ) DString_ToMBS( str );
}
#endif
/* Set dC->type before calling to instruct this function what type number to convert: */
int ConvertStringToNumber( DaoProcess *proc, DaoValue *dA, DaoValue *dC )
{
	DaoLexer *lexer;
	DaoParser *parser;
	DaoToken *tok, **tokens;
	DString *mbs = proc->mbstring;
	int tid = dC->type;
	int tokid = 0;
	int ec, sign = 1;

	if( dA->type != DAO_STRING || tid == DAO_NONE || tid > DAO_LONG ) return 0;

	if( dA->xString.data->mbs ){
		DString_SetDataMBS( mbs, dA->xString.data->mbs, dA->xString.data->size );
	}else{
		DString_SetDataWCS( mbs, dA->xString.data->wcs, dA->xString.data->size );
	}
	DString_Trim( mbs );
	if( mbs->size ==0 ) return 0;

	parser = DaoVmSpace_AcquireParser( proc->vmSpace );
	lexer = parser->lexer;

	DaoLexer_Tokenize( lexer, mbs->mbs, DAO_LEX_COMMENT|DAO_LEX_SPACE );
	tokens = lexer->tokens->items.pToken;

	if( lexer->tokens->size == 0 ) goto ReturnFalse;
	switch( tokens[tokid]->name ){
	case DTOK_ADD : tokid += 1; break;
	case DTOK_SUB : tokid += 1; sign = -1; break;
	}
	if( tokid >= lexer->tokens->size ) goto ReturnFalse;
	tok = tokens[tokid++];

	switch( tid ){
	case DAO_INTEGER :
		if( tok->name < DTOK_DIGITS_DEC || tok->name > DTOK_DOUBLE_DEC ) goto ReturnFalse;
		if( sizeof(daoint) == 4 ){
			dC->xInteger.value = strtol( tok->string.mbs, 0, 0 );
		}else{
			dC->xInteger.value = strtoll( tok->string.mbs, 0, 0 );
		}
		if( sign <0 ) dC->xInteger.value = - dC->xInteger.value;
		break;
	case DAO_FLOAT :
		if( tok->name < DTOK_DIGITS_DEC || tok->name > DTOK_NUMBER_SCI ) goto ReturnFalse;
		dC->xFloat.value = strtod( tok->string.mbs, 0 );
		if( sign <0 ) dC->xFloat.value = - dC->xFloat.value;
		break;
	case DAO_DOUBLE :
		if( tok->name < DTOK_DIGITS_DEC || tok->name > DTOK_NUMBER_SCI ) goto ReturnFalse;
		dC->xDouble.value = strtod( tok->string.mbs, 0 );
		if( sign <0 ) dC->xDouble.value = - dC->xDouble.value;
		break;
	case DAO_COMPLEX :
		dC->xComplex.value.real = 0.0;
		dC->xComplex.value.imag = 0.0;
		if( tok->name >= DTOK_DIGITS_DEC && tok->name <= DTOK_NUMBER_SCI ){
			dC->xComplex.value.real = strtod( tok->string.mbs, 0 );
			if( sign <0 ) dC->xComplex.value.real = - dC->xComplex.value.real;

			if( tokid >= lexer->tokens->size ) goto ReturnTrue;
			tok = tokens[tokid];
			switch( tok->name ){
			case DTOK_ADD : tokid += 1; break;
			case DTOK_SUB : tokid += 1; sign = -1; break;
			default : goto ReturnFalse;
			}
			if( tokid >= lexer->tokens->size ) goto ReturnFalse;
			tok = tokens[tokid++];
		}
		if( tok->name != DTOK_NUMBER_IMG ) goto ReturnFalse;
		dC->xComplex.value.imag = strtod( tok->string.mbs, 0 );
		if( sign <0 ) dC->xComplex.value.imag = - dC->xComplex.value.imag;
		break;
#ifdef DAO_WITH_LONGINT
	case DAO_LONG :
		ec = DLong_FromString( dC->xLong.value, mbs );
		if( ec ){
			const char *msg = ec == 'L' ? "invalid radix" : "invalid digit";
			DaoProcess_RaiseException( proc, DAO_ERROR_VALUE, msg );
			return 0;
		}
		dC->xLong.value->sign = sign;
		break;
#endif
	}
	if( tokid < lexer->tokens->size ) goto ReturnFalse;

ReturnTrue:
	DaoVmSpace_ReleaseParser( proc->vmSpace, parser );
	return 1;
ReturnFalse:
	DaoVmSpace_ReleaseParser( proc->vmSpace, parser );
	return 0;
}
#ifdef DAO_WITH_NUMARRAY
static DaoArray* DaoProcess_PrepareArray( DaoProcess *self, DaoValue *dC, int etype )
{
	DaoArray *array = NULL;
	if( dC && dC->type == DAO_ARRAY && dC->xArray.refCount == 1 && array->original == NULL ){
		array = (DaoArray*) dC;
		DaoArray_SetNumType( array, etype );
	}else{
		array = DaoProcess_NewArray( self, etype );
	}
	return array;
}
#endif
static DaoTuple* DaoProcess_PrepareTuple( DaoProcess *self, DaoValue *dC, DaoType *ct, int size )
{
	DaoTuple *tuple = NULL;

	if( size < (ct->nested->size - ct->variadic) ) return NULL;
	if( ct->variadic == 0 ) size = ct->nested->size;

	if( dC && dC->type == DAO_TUPLE && dC->xTuple.unitype == ct ){
		if( dC->xTuple.size == size && dC->xTuple.refCount == 1 ) return (DaoTuple*) dC;
	}
	tuple = DaoProcess_NewTuple( self, size );
	tuple->unitype = ct;
	GC_IncRC( ct );
	return tuple;
}
DaoValue* DaoTypeCast( DaoProcess *proc, DaoType *ct, DaoValue *dA, DaoValue *dC )
{
	DaoNamespace *ns = proc->activeNamespace;
	DaoTuple *tuple = NULL, *tuple2 = NULL;
	DaoList *list = NULL, *list2 = NULL;
	DaoMap *map = NULL, *map2 = NULL;
	DaoType *tp = NULL, *tp2 = NULL;
	DaoArray *array = NULL, *array2 = NULL;
	DaoValue **data, **data2, *K = NULL, *V = NULL;
	DaoValue *itvalue;
	DString *str;
	DNode *node;
	daoint i, n, size;
	int type, variadic, tsize;
	if( ct == NULL ) goto FailConversion;
	if( ct->tid == DAO_ANY ) goto Rebind;
	if( dA->type == ct->tid && ct->tid >= DAO_INTEGER && ct->tid < DAO_ARRAY ) goto Rebind;
	if( ct->tid > DAO_NONE && ct->tid <= DAO_STRING && (dC == NULL || dC->type != ct->tid) ){
		dC = DaoValue_SimpleCopy( ct->value );
		DaoProcess_CacheValue( proc, dC );
	}
	if( dA->type == DAO_STRING && ct->tid > DAO_NONE && ct->tid <= DAO_LONG ){
		if( ConvertStringToNumber( proc, dA, dC ) ==0 ) goto FailConversion;
		return dC;
	}
	switch( ct->tid ){
	case DAO_INTEGER :
		dC->xInteger.value = DaoValue_GetInteger( dA );
		break;
	case DAO_FLOAT :
		dC->xFloat.value = DaoValue_GetFloat( dA );
		break;
	case DAO_DOUBLE :
		dC->xDouble.value = DaoValue_GetDouble( dA );
		break;
	case DAO_COMPLEX :
		if( dA->type == DAO_COMPLEX ) goto Rebind;
		if( dA->type >= DAO_ARRAY ) goto FailConversion;
		dC->xComplex.value = DaoValue_GetComplex( dA );
		break;
	case DAO_LONG :
		if( dA->type == DAO_LONG ) goto Rebind;
		if( dA->type >= DAO_ARRAY ) goto FailConversion;
#ifdef DAO_WITH_LONGINT
		DLong_FromValue( dC->xLong.value, dA );
		dC->type = DAO_LONG;
#else
		goto FailConversion;
#endif
		break;
	case DAO_STRING :
		if( dA->type == DAO_STRING ) goto Rebind;
		str = dC->xString.data;
		if( dA->type < DAO_ARRAY ){
			DaoValue_GetString( dA, str );
#ifdef DAO_WITH_NUMARRAY
		}else if( dA->type == DAO_ARRAY ){
			array = (DaoArray*) dA;
			if( array->original && DaoArray_Sliced( array ) == 0 ) goto FailConversion;
			DaoArray_ToString( array, str, 0, array->size );
#endif
		}else if( dA->type == DAO_LIST ){
			list = & dA->xList;
			DString_ToWCS( str );
			DString_Resize( str, list->items.size );
			type = 1; /*MBS*/
			for(i=0,n=list->items.size; i<n; i++){
				itvalue = list->items.items.pValue[i];
				if( itvalue->type > DAO_DOUBLE ) goto FailConversion;
				str->wcs[i] = DaoValue_GetInteger( itvalue );
				if( str->wcs[i] > 255 ) type = 0;
			}
			if( type ) DString_ToMBS( str );
		}else{
			goto FailConversion;
		}
		break;
#ifdef DAO_WITH_NUMARRAY
	case DAO_ARRAY :
		if( ct->nested->size >0 ) tp = ct->nested->items.pType[0];
		if( dA->type == DAO_STRING ){
			str = dA->xString.data;
			if( tp->tid < DAO_INTEGER || tp->tid > DAO_DOUBLE ) goto FailConversion;
			array = DaoProcess_PrepareArray( proc, dC, tp->tid );
			DaoArray_ResizeVector( array, str->size );
			for(i=0,n=str->size; i<n; i++){
				wchar_t ch = str->mbs ? str->mbs[i] : str->wcs[i];
				switch( tp->tid ){
				case DAO_INTEGER : array->data.i[i] = ch; break;
				case DAO_FLOAT   : array->data.f[i]  = ch; break;
				case DAO_DOUBLE  : array->data.d[i]  = ch; break;
				default : break;
				}
			}
		}else if( dA->type == DAO_ARRAY ){
			if( tp == NULL ) goto Rebind;
			if( tp->tid & DAO_ANY ) goto Rebind;
			if( array2->etype == tp->tid ) goto Rebind;
			if( tp->tid < DAO_INTEGER || tp->tid > DAO_COMPLEX ) goto FailConversion;
			array2 = & dA->xArray;
			if( array2->original && DaoArray_Sliced( array2 ) == 0 ) goto FailConversion;

			array = DaoProcess_PrepareArray( proc, dC, tp->tid );
			DaoArray_ResizeArray( array, array2->dims, array2->ndim );
			for(i=0,size=array2->size; i<size; i++){
				switch( array->etype ){
				case DAO_INTEGER : array->data.i[i] = DaoArray_GetInteger( array2, i ); break;
				case DAO_FLOAT   : array->data.f[i] = DaoArray_GetFloat( array2, i );  break;
				case DAO_DOUBLE  : array->data.d[i] = DaoArray_GetDouble( array2, i ); break;
				case DAO_COMPLEX : array->data.c[i] = DaoArray_GetComplex( array2, i ); break;
				}
			}
		}else if( dA->type == DAO_LIST ){
			list = & dA->xList;
			size = list->items.size;
			if( tp == NULL ) goto FailConversion;
			if( tp->tid == DAO_NONE || tp->tid > DAO_COMPLEX ) goto FailConversion;
			array = DaoProcess_PrepareArray( proc, dC, tp->tid );
			DaoArray_ResizeVector( array, size );
			for(i=0; i<size; i++){
				itvalue = list->items.items.pValue[i];
				if( itvalue->type > DAO_COMPLEX ) goto FailConversion;
				switch( array->etype ){
				case DAO_INTEGER : array->data.i[i] = DaoValue_GetInteger( itvalue ); break;
				case DAO_FLOAT   : array->data.f[i] = DaoValue_GetFloat( itvalue );  break;
				case DAO_DOUBLE  : array->data.d[i] = DaoValue_GetDouble( itvalue ); break;
				case DAO_COMPLEX : array->data.c[i] = DaoValue_GetComplex( itvalue ); break;
				}
			}
		}else goto FailConversion;
		dC = (DaoValue*) array;
		break;
#endif
	case DAO_LIST :
		if( DaoType_MatchValue( ct, dA, NULL ) >= DAO_MT_EQ ) goto Rebind;
		if( ct->nested->size >0 ) tp = ct->nested->items.pType[0];

		if( tp == NULL ) goto FailConversion;
		if( dC && dC->type == DAO_LIST && dC->xList.refCount == 1 && dC->xList.unitype == ct ){
			list = (DaoList*) dC;
		}else{
			list = DaoProcess_NewList( proc );
			list->unitype = ct;
			GC_IncRC( ct );
			dC = (DaoValue*) list;
		}
		if( dA->type == DAO_STRING ){
			str = dA->xString.data;
			if( tp->tid < DAO_INTEGER || tp->tid > DAO_DOUBLE ) goto FailConversion;
			DArray_Resize( & list->items, DString_Size( str ), tp->value );
			data = list->items.items.pValue;
			for(i=0,n=str->size; i<n; i++){
				wchar_t ch = str->mbs ? str->mbs[i] : str->wcs[i];
				switch( tp->tid ){
				case DAO_INTEGER : data[i]->xInteger.value = ch; break;
				case DAO_FLOAT   : data[i]->xFloat.value = ch; break;
				case DAO_DOUBLE  : data[i]->xDouble.value = ch; break;
				default : break;
				}
			}
#ifdef DAO_WITH_NUMARRAY
		}else if( dA->type == DAO_ARRAY ){
			array = (DaoArray*)dA;
			if( tp->tid < DAO_INTEGER || tp->tid > DAO_COMPLEX ) goto FailConversion;
			if( array->original && DaoArray_Sliced( array ) == 0 ) goto FailConversion;
			DArray_Resize( & list->items, array->size, tp->value );
			data = list->items.items.pValue;
			for(i=0,n=array->size; i<n; i++){
				switch( tp->tid ){
				case DAO_INTEGER : data[i]->xInteger.value = DaoArray_GetInteger( array, i ); break;
				case DAO_FLOAT   : data[i]->xFloat.value = DaoArray_GetFloat( array, i );  break;
				case DAO_DOUBLE  : data[i]->xDouble.value = DaoArray_GetDouble( array, i ); break;
				case DAO_COMPLEX : data[i]->xComplex.value = DaoArray_GetComplex( array, i ); break;
				}
			}
#endif
		}else if( dA->type == DAO_LIST ){
			list2 = & dA->xList;
			DArray_Resize( & list->items, list2->items.size, NULL );
			data = list->items.items.pValue;
			data2 = list2->items.items.pValue;
			for(i=0,n=list2->items.size; i<n; i++ ){
				V = DaoTypeCast( proc, tp, data2[i], V );
				if( V == NULL || V->type ==0 ) goto FailConversion;
				DaoValue_Copy( V, data + i );
			}
		}else if( dA->type == DAO_TUPLE ){
			tuple2 = (DaoTuple*) dA;
			DArray_Resize( & list->items, tuple2->size, NULL );
			data = list->items.items.pValue;
			data2 = tuple2->items;
			for(i=0,n=tuple2->size; i<n; i++ ){
				V = DaoTypeCast( proc, tp, data2[i], V );
				if( V == NULL || V->type ==0 ) goto FailConversion;
				DaoValue_Copy( V, data + i );
			}
		}else goto FailConversion;
		break;
	case DAO_MAP :
		if( dA->type != DAO_MAP ) goto FailConversion;
		map2 = & dA->xMap;
		if( DaoType_MatchTo( map2->unitype, ct, NULL ) >= DAO_MT_EQ ) goto Rebind;

		if( dC && dC->type == DAO_MAP && dC->xMap.refCount == 1 && dC->xMap.unitype == ct ){
			map = (DaoMap*) dC;
			DMap_Reset( map->items );
		}else{
			map = DaoProcess_NewMap( proc, map2->items->hashing );
			map->unitype = ct;
			GC_IncRC( ct );
			dC = (DaoValue*) map;
		}
		if( ct->nested->size >0 ) tp = ct->nested->items.pType[0];
		if( ct->nested->size >1 ) tp2 = ct->nested->items.pType[1];
		if( tp == NULL || tp2 == NULL ) goto FailConversion;
		node = DMap_First( map2->items );
		for(; node!=NULL; node=DMap_Next(map2->items,node) ){
			K = DaoTypeCast( proc, tp, node->key.pValue, K );
			V = DaoTypeCast( proc, tp2, node->value.pValue, V );
			if( K ==NULL || V ==NULL || K->type ==0 || V->type ==0 ) goto FailConversion;
			DMap_Insert( map->items, K, V );
		}
		break;
	case DAO_TUPLE :
		tsize = ct->nested->size - ct->variadic;
		if( dA->type == DAO_TUPLE ){
			tuple2 = (DaoTuple*) dA;
			if( tuple2->unitype == ct || tsize == 0 ) goto Rebind;
			tuple = DaoProcess_PrepareTuple( proc, dC, ct, tuple2->size );
			if( tuple == NULL ) goto FailConversion;
			for(i=0; i<tuple->size; i++){
				DaoValue *V = tuple2->items[i];
				tp2 = dao_type_any;
				if( i < tsize ) tp2 = ct->nested->items.pType[i];
				if( tp2->tid == DAO_PAR_NAMED ) tp2 = & tp2->aux->xType;
				V = DaoTypeCast( proc, tp2, V, K );
				if( V == NULL || V->type == 0 ) goto FailConversion;
				DaoValue_Copy( V, tuple->items + i );
			}
		}else if( dA->type == DAO_LIST ){
			list = (DaoList*) dA;
			tuple = DaoProcess_PrepareTuple( proc, dC, ct, list->items.size );
			if( tuple == NULL ) goto FailConversion;
			for(i=0; i<tuple->size; i++){
				DaoValue *V = list->items.items.pValue[i];
				tp2 = dao_type_any;
				if( i < tsize ) tp2 = ct->nested->items.pType[i];
				if( tp2->tid == DAO_PAR_NAMED ) tp2 = & tp2->aux->xType;
				V = DaoTypeCast( proc, tp2, V, K );
				if( V == NULL || V->type == 0 ) goto FailConversion;
				DaoValue_Copy( V, tuple->items + i );
			}
		}else{
			goto FailConversion;
		}
		dC = (DaoValue*) tuple;
		break;
	case DAO_CLASS :
		if( dA == NULL || dA->type != DAO_CLASS ) goto FailConversion;
		if( ct->aux == NULL ) goto Rebind; /* to "class"; */
		dC = DaoClass_CastToBase( (DaoClass*)dA, ct );
		if( dC == NULL ) goto FailConversion;
		break;
	case DAO_OBJECT :
		if( dA->type == DAO_CDATA || dA->type == DAO_CSTRUCT ) dA = (DaoValue*) dA->xCdata.object;
		/* XXX compiling time checking??? */
		if( dA == NULL || dA->type != DAO_OBJECT ) goto FailConversion;
		dC = DaoObject_CastToBase( & dA->xObject, ct );
		if( dC == NULL ) goto FailConversion;
		break;
	case DAO_CTYPE :
		if( dA->type == DAO_CLASS ){
			dC = DaoClass_CastToBase( (DaoClass*)dA, ct );
		}else if( dA->type == DAO_CTYPE ){
			if( DaoType_ChildOf( dA->xCtype.ctype, ct ) ) dC = dA;
		}
		if( dC == NULL ) goto FailConversion;
		break;
	case DAO_CDATA :
	case DAO_CSTRUCT :
		dC = NULL;
		if( dA->type == DAO_CDATA || dA->type == DAO_CSTRUCT ){
			if( DaoType_ChildOf( dA->xCdata.ctype, ct ) ) dC = dA;
		}else if( dA->type == DAO_OBJECT ){
			dC = DaoObject_CastToBase( & dA->xObject, ct );
		}
		if( dC == NULL ) goto FailConversion;
		break;
	case DAO_VALTYPE :
		if( DaoValue_Compare( ct->aux, dA ) != 0 ) goto FailConversion;
		dC = dA;
		break;
	case DAO_VARIANT :
		dC = dA;
		break;
	default : break;
	}
	return dC;
Rebind :
	return dA;
FailConversion :
	return NULL;
}

DaoRoutine* DaoValue_Check( DaoRoutine *self, DaoType *selftp, DaoType *ts[], int np, int code, DArray *es );
void DaoPrintCallError( DArray *errors, DaoStream *stdio );

void DaoProcess_ShowCallError( DaoProcess *self, DaoRoutine *rout, DaoValue *selfobj, DaoValue *ps[], int np, int codemode )
{
	DaoStream *ss = DaoStream_New();
	DaoNamespace *ns = self->activeNamespace;
	DaoType *selftype = selfobj ? DaoNamespace_GetType( ns, selfobj ) : NULL;
	DaoType *ts[DAO_MAX_PARAM];
	DArray *errors = DArray_New(0);
	int i;
	for(i=0; i<np; i++) ts[i] = DaoNamespace_GetType( ns, ps[i] );
	DaoValue_Check( rout, selftype, ts, np, codemode, errors );
	ss->attribs |= DAO_IO_STRING;
	DaoPrintCallError( errors, ss );
	DArray_Delete( errors );
	DaoProcess_RaiseException( self, DAO_ERROR_PARAM, ss->streamString->mbs );
	DaoStream_Delete( ss );
}

int DaoRoutine_SetVmCodes2( DaoRoutine *self, DVector *vmCodes );

static void DaoProcess_MapTypes( DaoProcess *self, DMap *deftypes )
{
	DaoRoutine *routine = self->activeRoutine;
	DNode *it = DMap_First(routine->body->localVarType);
	for(; it; it = DMap_Next(routine->body->localVarType,it) ){
		DaoValue *V = self->activeValues[ it->key.pInt ];
		if( V == NULL || V->type != DAO_TYPE || it->value.pType->tid != DAO_TYPE ) continue;
		MAP_Insert( deftypes, it->value.pType->nested->items.pType[0], V );
	}
}

void DaoProcess_MakeRoutine( DaoProcess *self, DaoVmCode *vmc )
{
	DaoType *tp;
	DaoValue **pp2;
	DaoValue **pp = self->activeValues + vmc->a + 1;
	DaoRoutine *proto = (DaoRoutine*) self->activeValues[vmc->a];
	DaoRoutine *closure;
	DMap *deftypes;
	int i, j, k, m, K;
	if( proto->body->vmCodes->size ==0 && proto->body->annotCodes->size ){
		if( DaoRoutine_SetVmCodes( proto, proto->body->annotCodes ) ==0 ){
			DaoProcess_RaiseException( self, DAO_ERROR, "invalid closure" );
			return;
		}
	}
	if( proto->body->svariables->size == 0 && vmc->b == 0 ){
		DaoProcess_SetValue( self, vmc->c, (DaoValue*) proto );
		if( proto->attribs & DAO_ROUT_DEFERRED ) DArray_Append( self->defers, proto );
		return;
	}

	closure = DaoRoutine_Copy( proto, 1, 1, 1 );
	pp2 = closure->routConsts->items.items.pValue;

	K = vmc->b - closure->body->svariables->size;
	for(j=0,k=0; j<closure->parCount; j+=1){
		DaoType *partype = closure->routType->nested->items.pType[j];
		if( partype->tid != DAO_PAR_DEFAULT ) continue;
		if( closure->routConsts->items.items.pValue[j] != NULL ) continue;
		if( k >= K ) break;
		DaoValue_Copy( pp[k], pp2 + j );
		k += 1;
	}
	m = (proto->attribs & DAO_ROUT_PASSRET) != 0;
	for(j=m; j<closure->body->svariables->size; ++j){
		DaoVariable_Set( closure->body->svariables->items.pVar[j], pp[k+j-m], NULL );
	}

	tp = DaoNamespace_MakeRoutType( self->activeNamespace, closure->routType, pp2, NULL, NULL );
	GC_ShiftRC( tp, closure->routType );
	closure->routType = tp;

	deftypes = DMap_New(0,0);
	DaoProcess_MapTypes( self, deftypes );
	tp = DaoType_DefineTypes( closure->routType, closure->nameSpace, deftypes );
	GC_ShiftRC( tp, closure->routType );
	closure->routType = tp;
	DaoRoutine_MapTypes( closure, deftypes );
	DMap_Delete( deftypes );

	/* It's necessary to put it in "self" process in any case, so that it can be GC'ed: */
	DaoProcess_SetValue( self, vmc->c, (DaoValue*) closure );
	DArray_Assign( closure->body->annotCodes, proto->body->annotCodes );
	if( DaoRoutine_SetVmCodes2( closure, proto->body->vmCodes ) ==0 ){
		DaoProcess_RaiseException( self, DAO_ERROR, "function creation failed" );
	}
	if( proto->attribs & DAO_ROUT_DEFERRED ) DArray_Append( self->defers, closure );
#if 0
	DaoRoutine_PrintCode( proto, self->vmSpace->stdioStream );
	DaoRoutine_PrintCode( closure, self->vmSpace->stdioStream );
	printf( "%s\n", closure->routType->name->mbs );
#endif
}




void STD_Debug( DaoProcess *proc, DaoValue *p[], int N );

void DaoProcess_RaiseException( DaoProcess *self, int type, const char *value )
{
	DaoType *etype;
	DaoType *warning = DaoException_GetType( DAO_WARNING );
	DaoStream *stream = self->vmSpace->errorStream;
	DaoException *except;

#ifdef DEBUG
	if( self->topFrame->routine->body ){
		DaoVmCode *vmc = self->activeCode;
		int i = vmc - self->topFrame->codes;
		if( self->topFrame->routine != self->activeRoutine ) i = self->topFrame->entry;
		if( i >= 0 && i < self->topFrame->routine->body->annotCodes->size )
			DaoVmCodeX_Print( *self->topFrame->routine->body->annotCodes->items.pVmc[i], NULL );
	}
#endif

	if( type <= 1 ) return;
	if( type >= ENDOF_BASIC_EXCEPT ) type = DAO_ERROR;
	if( self->activeRoutine == NULL ) return; // TODO: Error infor;

	etype = DaoException_GetType( type );
	if( DaoType_ChildOf( etype, warning ) ){
		/* XXX support warning suppression */
		except = DaoException_New( etype );
		DaoException_Init( except, self, value );
		DaoException_Print( except, stream );
		DaoException_Delete( except );
		return;
	}
	except = DaoException_New( etype );
	DaoException_Init( except, self, value );
	DArray_Append( self->exceptions, (DaoValue*) except );
	if( (self->vmSpace->options & DAO_OPTION_DEBUG) ){
		if( self->stopit ==0 && self->vmSpace->stopit ==0 ){
			DaoProcess_Trace( self, 10 );
			DaoProcess_PrintException( self, NULL, 0 );
			STD_Debug( self, NULL, 0 );
		}
	}
}
void DaoProcess_RaiseTypeError( DaoProcess *self, DaoType *from, DaoType *to, const char *op )
{
	DString *details = DString_New(1);
	if( from == NULL ) from = dao_type_udf;
	if( to == NULL ) to = dao_type_udf;
	DString_AppendMBS( details, op );
	DString_AppendMBS( details, " from \'" );
	DString_Append( details,  from->name );
	DString_AppendMBS( details, "\' to \'" );
	DString_Append( details,  to->name );
	DString_AppendMBS( details, "\'." );
	DaoProcess_RaiseException( self, DAO_ERROR_TYPE, details->mbs );
	DString_Delete( details );
}

void DaoProcess_PrintException( DaoProcess *self, DaoStream *stream, int clear )
{
	DaoType *extype = dao_Exception_Typer.core->kernel->abtype;
	DaoValue **excobjs = self->exceptions->items.pValue;
	int i, n;

	if( stream == NULL ) stream = self->vmSpace->errorStream;
	for(i=0,n=self->exceptions->size; i<n; i++){
		DaoException *except = NULL;
		if( excobjs[i]->type == DAO_CSTRUCT ){
			except = (DaoException*) excobjs[i];
		}else if( excobjs[i]->type == DAO_OBJECT ){
			except = (DaoException*)DaoObject_CastToBase( & excobjs[i]->xObject, extype );
		}
		if( except == NULL ) continue;
		DaoException_Print( except, stream );
	}
	if( clear ) DArray_Clear( self->exceptions );
}


DaoValue* DaoProcess_MakeConst( DaoProcess *self )
{
	DaoVmCodeX vmcx = {0,0,0,0,0,0,0,0,0};
	DaoVmCode *vmc = self->activeCode;

	self->activeValues = self->stackValues;
	DVector_Clear( self->activeRoutine->body->vmCodes );
	DVector_PushCode( self->activeRoutine->body->vmCodes, *vmc );
	if( self->activeRoutine->body->annotCodes->size == 0 )
		DArray_Append( self->activeRoutine->body->annotCodes, & vmcx );

	/*
	// DaoProcess_PopFrame() and DaoProcess_SetActiveFrame() will be called
	// for C function calls, and self->activeTypes will be updated to the
	// frame's ::codes and ::types.
	*/
	self->activeCode = self->activeRoutine->body->vmCodes->data.codes;
	self->topFrame->codes = self->activeCode;
	self->topFrame->types = self->activeTypes;
	vmc = self->activeCode;
	switch( vmc->code ){
	case DVM_ADD : case DVM_SUB : case DVM_MUL :
	case DVM_DIV : case DVM_MOD : case DVM_POW :
		DaoProcess_DoBinArith( self, vmc );
		break;
	case DVM_AND : case DVM_OR : case DVM_LT :
	case DVM_LE :  case DVM_EQ : case DVM_NE :
		DaoProcess_DoBinBool( self, vmc );
		break;
	case DVM_IN :
		DaoProcess_DoInTest( self, vmc );
		break;
	case DVM_NOT : case DVM_MINUS :
		DaoProcess_DoUnaArith( self, vmc ); break;
	case DVM_BITAND : case DVM_BITOR : case DVM_BITXOR :
		DaoProcess_DoBitLogic( self, vmc ); break;
	case DVM_BITLFT : case DVM_BITRIT :
		DaoProcess_DoBitShift( self, vmc ); break;
	case DVM_TILDE :
		DaoProcess_DoBitFlip( self, vmc ); break;
	case DVM_CHECK :
		DaoProcess_DoCheck( self, vmc ); break;
	case DVM_NAMEVA :
		DaoProcess_BindNameValue( self, vmc ); break;
	case DVM_PAIR :
		DaoProcess_DoPair( self, vmc ); break;
	case DVM_TUPLE :
		DaoProcess_DoTuple( self, vmc ); break;
	case DVM_GETI :
	case DVM_GETMI :
		DaoProcess_DoGetItem( self, vmc ); break;
	case DVM_GETF :
		DaoProcess_DoGetField( self, vmc ); break;
	case DVM_SETI :
	case DVM_SETMI :
		DaoProcess_DoSetItem( self, vmc ); break;
	case DVM_SETF :
		DaoProcess_DoSetField( self, vmc ); break;
	case DVM_LIST :
		DaoProcess_DoList( self, vmc ); break;
	case DVM_MAP :
	case DVM_HASH :
		DaoProcess_DoMap( self, vmc ); break;
	case DVM_VECTOR :
		DaoProcess_DoVector( self, vmc ); break;
	case DVM_MATRIX :
		DaoProcess_DoMatrix( self, vmc ); break;
	case DVM_MATH :
		DaoVM_DoMath( self, vmc, self->activeValues[ vmc->c ], self->activeValues[1] );
		break;
	case DVM_PACK :
	case DVM_MPACK :
		DaoProcess_DoPacking( self, vmc );
		break;
	case DVM_CALL :
	case DVM_MCALL :
		DaoProcess_DoCall( self, vmc );
		break;
	default: break;
	}
	if( self->status == DAO_PROCESS_STACKED ){
		self->topFrame->returning = -1;
		DaoProcess_Execute( self );
	}
	if( self->exceptions->size >0 ) return NULL;

	/* avoid GC */
	/* DArray_Clear( self->regArray ); */
	return self->stackValues[ vmc->c ];
}

static void DaoProcess_AdjustCodes( DaoProcess *self, int options )
{
	DaoUserHandler *handler = self->vmSpace->userHandler;
	DaoRoutine *routine = self->topFrame->routine;
	DaoVmCode *c = self->topFrame->codes;
	int i, n = routine->body->vmCodes->size;
	int mode = routine->body->mode;
	if( options & DAO_OPTION_DEBUG ){
		routine->body->mode |= DAO_OPTION_DEBUG;
		if( handler && handler->BreakPoints ) handler->BreakPoints( handler, routine );
	}else if( mode & DAO_OPTION_DEBUG ){
		routine->body->mode &= ~DAO_OPTION_DEBUG;
		for(i=0; i<n; i++) if( c[i].code == DVM_DEBUG ) c[i].code = DVM_NOP;
	}
	if( (options & DAO_OPTION_SAFE) == (mode & DAO_OPTION_SAFE) ) return;
	if( options & DAO_OPTION_SAFE ){
		routine->body->mode |= DAO_OPTION_SAFE;
		for(i=0; i<n; i++) if( c[i].code == DVM_GOTO ) c[i].code = DVM_SAFE_GOTO;
	}else if( mode & DAO_OPTION_SAFE ){
		routine->body->mode &= ~DAO_OPTION_SAFE;
		for(i=0; i<n; i++) if( c[i].code == DVM_SAFE_GOTO ) c[i].code = DVM_GOTO;
	}
}

void DaoProcess_SetStdio( DaoProcess *self, DaoStream *stream )
{
	GC_ShiftRC( stream, self->stdioStream );
	self->stdioStream = stream;
}

DaoProcessAux* DaoProcessAux_New()
{
	int i;
	DaoProcessAux *self = (DaoProcessAux*) dao_malloc( sizeof(DaoProcessAux) );
	self->regexCaches = DHash_New(D_STRING,0);
	self->mtIndex = 0;
	self->mtBuffer[0] = rand();
	for(i=1; i<DAO_MTCOUNT; ++i){
		uint_t prev = self->mtBuffer[i-1];
		self->mtBuffer[i] = 0x6c078965 * (prev ^ (prev>>30)) + i;
	}
	return self;
}
void DaoProcessAux_Delete( DaoProcessAux *self )
{
	DNode *it = DMap_First( self->regexCaches );
	for( ; it !=NULL; it = DMap_Next(self->regexCaches, it) ) dao_free( it->value.pVoid );
	DMap_Delete( self->regexCaches );
}
void DaoProcessAux_GenerateMT( DaoProcessAux *self )
{
	uint_t i, *mtnums = self->mtBuffer;
	for(i=1; i<DAO_MTCOUNT; ++i){
		uint_t y = (mtnums[i] & 0x80000000) + (mtnums[(i+1)%DAO_MTCOUNT] & 0x7fffffff);
		mtnums[i] = mtnums[(i+397)%DAO_MTCOUNT] ^ (y >> 1);
		if( y % 2 ) mtnums[i] ^= 0x9908b0df;
	}
}
uint_t DaoProcessAux_ExtractMT( DaoProcessAux *self )
{
	uint_t y;
	if( self->mtIndex == 0 ) DaoProcessAux_GenerateMT( self );
	y = self->mtBuffer[ self->mtIndex ];
	y ^= y>>11;
	y ^= (y<<7) & 0x9d2c5680;
	y ^= (y<<15) & 0xefc60000;
	y ^= y>>18;
	self->mtIndex = (self->mtIndex + 1) % DAO_MTCOUNT;
	return y;
}
double DaoProcess_Random( DaoProcess *self )
{
	if( self->aux == NULL ) self->aux = DaoVmSpace_AcquireProcessAux( self->vmSpace );
	return DaoProcessAux_ExtractMT( self->aux ) / (double) 0xffffffff;
}


DaoRegex* DaoProcess_MakeRegex( DaoProcess *self, DString *src, int mbs )
{
#ifdef DAO_WITH_REGEX
	DaoRegex *pat = NULL;
	DaoRgxItem *it;
	DNode *node;
	char buf[50];
	int i;
	if( mbs && src->wcs ) DString_ToMBS( src );
	if( mbs==0 && src->mbs ) DString_ToWCS( src );
	DString_Trim( src );
	if( src->size ==0 ){
		if( self->activeRoutine )
			DaoProcess_RaiseException( self, DAO_ERROR, "pattern with empty string" );
		return NULL;
	}
	if( self->aux == NULL ) self->aux = DaoVmSpace_AcquireProcessAux( self->vmSpace );
	node = DMap_Find( self->aux->regexCaches, src );
	if( node ) return (DaoRegex*) node->value.pVoid;

	pat = DaoRegex_New( src );
	for( i=0; i<pat->count; i++ ){
		it = pat->items + i;
		if( it->type ==0 ){
			sprintf( buf, "incorrect pattern, at char %i.", it->length );
			if( self->activeRoutine ) DaoProcess_RaiseException( self, DAO_ERROR, buf );
			DaoRegex_Delete( pat );
			return NULL;
		}
	}
	DMap_Insert( self->aux->regexCaches, src, pat );
	return pat;
#else
	DaoProcess_RaiseException( self, DAO_ERROR, getCtInfo( DAO_DISABLED_REGEX ) );
	return NULL;
#endif
}
