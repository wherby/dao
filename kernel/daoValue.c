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

#include"stdlib.h"
#include"string.h"

#include"daoGC.h"
#include"daoType.h"
#include"daoStdtype.h"
#include"daoStream.h"
#include"daoRoutine.h"
#include"daoClass.h"
#include"daoObject.h"
#include"daoNumtype.h"
#include"daoNamespace.h"
#include"daoVmspace.h"
#include"daoParser.h"
#include"daoProcess.h"
#include"daoValue.h"


DaoConstant* DaoConstant_New( DaoValue *value )
{
	DaoConstant *self = (DaoConstant*) dao_calloc( 1, sizeof(DaoConstant) );
	DaoValue_Init( self, DAO_CONSTANT );
	DaoValue_Copy( value, & self->value );
	return self;
}
DaoVariable* DaoVariable_New( DaoValue *value, DaoType *type )
{
	DaoVariable *self = (DaoVariable*) dao_calloc( 1, sizeof(DaoVariable) );
	DaoValue_Init( self, DAO_VARIABLE );
	DaoValue_Move( value, & self->value, type );
	self->dtype = type;
	GC_IncRC( type );
	return self;
}

void DaoConstant_Delete( DaoConstant *self )
{
	GC_DecRC( self->value );
	dao_free( self );
}
void DaoVariable_Delete( DaoVariable *self )
{
	GC_DecRC( self->value );
	GC_DecRC( self->dtype );
	dao_free( self );
}
void DaoConstant_Set( DaoConstant *self, DaoValue *value )
{
	DaoValue_Copy( value, & self->value );
}
int DaoVariable_Set( DaoVariable *self, DaoValue *value, DaoType *type )
{
	if( type ){
		GC_ShiftRC( type, self->dtype );
		self->dtype = type;
	}
	return DaoValue_Move( value, & self->value, self->dtype );
}

#ifdef DAO_WITH_NUMARRAY
int DaoArray_Compare( DaoArray *x, DaoArray *y );
#endif

#define number_compare( x, y ) ((x)==(y) ? 0 : ((x)<(y) ? -1 : 1))

/* Invalid comparison returns either -100 or 100: */

int DaoComplex_Compare( DaoComplex *left, DaoComplex *right )
{
	if( left->value.real < right->value.real ) return -100;
	if( left->value.real > right->value.real ) return  100;
	if( left->value.imag < right->value.imag ) return -100;
	if( left->value.imag > right->value.imag ) return  100;
	return 0;
}

int DaoEnum_Compare( DaoEnum *L, DaoEnum *R )
{
	DaoEnum E;
	DNode *N = NULL;
	DMap *ML = L->etype->mapNames;
	DMap *MR = R->etype->mapNames;
	uchar_t FL = L->etype->flagtype;
	uchar_t FR = R->etype->flagtype;
	char SL = L->etype->name->mbs[0];
	char SR = R->etype->name->mbs[0];
	if( L->etype == R->etype ){
		return L->value == R->value ? 0 : ( L->value < R->value ? -1 : 1 );
	}else if( SL == '$' && SR == '$' && FL == 0 && FR == 0 ){
		return DString_Compare( L->etype->name, R->etype->name );
	}else if( SL == '$' && SR == '$' ){
		if( L->etype->mapNames->size != R->etype->mapNames->size ){
			return number_compare( L->etype->mapNames->size, R->etype->mapNames->size );
		}else{
			for(N=DMap_First(ML);N;N=DMap_Next(ML,N)){
				if( DMap_Find( MR, N->key.pVoid ) ==0 ) return -1;
			}
			return 0;
		}
	}else if( SL == '$' ){
		E.etype = R->etype;
		E.value = R->value;
		DaoEnum_SetValue( & E, L, NULL );
		return E.value == R->value ? 0 : ( E.value < R->value ? -1 : 1 );
	}else if( SR == '$' ){
		E.etype = L->etype;
		E.value = L->value;
		DaoEnum_SetValue( & E, R, NULL );
		return L->value == E.value ? 0 : ( L->value < E.value ? -1 : 1 );
	}else{
		return L->value == R->value ? 0 : ( L->value < R->value ? -1 : 1 );
	}
}
int DaoTuple_Compare( DaoTuple *lt, DaoTuple *rt )
{
	int i, lb, rb, res;
	if( lt->size < rt->size ) return -100;
	if( lt->size > rt->size ) return 100;

	for(i=0; i<lt->size; i++){
		DaoValue *lv = lt->items[i];
		DaoValue *rv = rt->items[i];
		int lb = lv ? lv->type : 0;
		int rb = rv ? rv->type : 0;
		if( lb == rb && lb == DAO_TUPLE ){
			res = DaoTuple_Compare( (DaoTuple*) lv, (DaoTuple*) rv );
			if( res != 0 ) return res;
		}else if( lb != rb || lb ==0 || lb > DAO_ARRAY || lb == DAO_COMPLEX ){
			if( lv < rv ){
				return -100;
			}else if( lv > rv ){
				return 100;
			}
		}else{
			res = DaoValue_Compare( lv, rv );
			if( res != 0 ) return res;
		}
	}
	return 0;
}
int DaoList_Compare( DaoList *list1, DaoList *list2 )
{
	DaoValue **d1 = list1->items.items.pValue;
	DaoValue **d2 = list2->items.items.pValue;
	int size1 = list1->items.size;
	int size2 = list2->items.size;
	int min = size1 < size2 ? size1 : size2;
	int res = size1 == size2 ? 1 : 100;
	int i = 0, cmp = 0;
	/* find the first unequal items */
	while( i < min && (cmp = DaoValue_Compare(*d1, *d2)) ==0 ) i++, d1++, d2++;
	if( i < min ){
		if( abs( cmp > 1 ) ) return cmp;
		return cmp * res;
	}
	if( size1 == size2  ) return 0;
	return size1 < size2 ? -100 : 100;
}
int DaoCstruct_Compare( DaoValue *left, DaoValue *right )
{
	if( left == right ) return 0;
	if( left->xCstruct.ctype != right->xCstruct.ctype ){
		return number_compare( (size_t)left->xCstruct.ctype, (size_t)right->xCstruct.ctype );
	}
	if( left->type == DAO_CDATA ){
		return number_compare( (size_t)left->xCdata.data, (size_t)right->xCdata.data );
	}
	return number_compare( (size_t)left, (size_t)right );
}
int DaoValue_Compare( DaoValue *left, DaoValue *right )
{
	double L, R;
	int res = 0;
	if( left == right ) return 0;
	if( left == NULL || right == NULL ) return left < right ? -100 : 100;
	if( left->type != right->type ){
		res = left->type < right->type ? -100 : 100;
		if( right->type == DAO_TUPLE && right->xTuple.subtype == DAO_PAIR ){
			if( (res = DaoValue_Compare( left, right->xTuple.items[0] )) <= 0 ) return res;
			if( (res = DaoValue_Compare( left, right->xTuple.items[1] )) >= 0 ) return res;
			return 0;
		}else if( left->type == DAO_TUPLE && left->xTuple.subtype == DAO_PAIR ){
			if( (res = DaoValue_Compare( left->xTuple.items[0], right )) >= 0 ) return res;
			if( (res = DaoValue_Compare( left->xTuple.items[1], right )) <= 0 ) return res;
			return 0;
		}
#ifdef DAO_WITH_LONGINT
		if( left->type == DAO_LONG && (right->type && right->type <= DAO_DOUBLE) ){
			if( right->type == DAO_INTEGER ){
				return DLong_CompareToInteger( left->xLong.value, right->xInteger.value );
			}
			return DLong_CompareToDouble( left->xLong.value, DaoValue_GetDouble( right ) );
		}else if( right->type == DAO_LONG && (left->type && left->type <= DAO_DOUBLE) ){
			if( left->type == DAO_INTEGER ){
				return - DLong_CompareToInteger( right->xLong.value, left->xInteger.value );
			}
			return - DLong_CompareToDouble( right->xLong.value, DaoValue_GetDouble( left ) );
		}
#endif
		if( left->type < DAO_INTEGER || left->type > DAO_DOUBLE ) return res;
		if( right->type < DAO_INTEGER || right->type > DAO_DOUBLE ) return res;
		L = DaoValue_GetDouble( left );
		R = DaoValue_GetDouble( right );
		return L == R ? 0 : (L < R ? -1 : 1);
	}
	switch( left->type ){
	case DAO_NONE : return 0;
	case DAO_INTEGER : return number_compare( left->xInteger.value, right->xInteger.value );
	case DAO_FLOAT   : return number_compare( left->xFloat.value, right->xFloat.value );
	case DAO_DOUBLE  : return number_compare( left->xDouble.value, right->xDouble.value );
	case DAO_COMPLEX : return DaoComplex_Compare( & left->xComplex, & right->xComplex );
#ifdef DAO_WITH_LONGINT
	case DAO_LONG    : return DLong_Compare( left->xLong.value, right->xLong.value );
#endif
	case DAO_STRING  : return DString_Compare( left->xString.data, right->xString.data );
	case DAO_ENUM    : return DaoEnum_Compare( & left->xEnum, & right->xEnum );
	case DAO_TUPLE   : return DaoTuple_Compare( & left->xTuple, & right->xTuple );
	case DAO_LIST    : return DaoList_Compare( & left->xList, & right->xList );
	case DAO_CDATA :
	case DAO_CSTRUCT :
	case DAO_CTYPE : return DaoCstruct_Compare( left, right );
#ifdef DAO_WITH_NUMARRAY
	case DAO_ARRAY   : return DaoArray_Compare( & left->xArray, & right->xArray );
#endif
	}
	return left < right ? -100 : 100; /* needed for map */
}
int DaoValue_IsZero( DaoValue *self )
{
	if( self == NULL ) return 1;
	switch( self->type ){
	case DAO_NONE : return 1;
	case DAO_INTEGER : return self->xInteger.value == 0;
	case DAO_FLOAT  : return self->xFloat.value == 0.0;
	case DAO_DOUBLE : return self->xDouble.value == 0.0;
	case DAO_COMPLEX : return self->xComplex.value.real == 0.0 && self->xComplex.value.imag == 0.0;
	case DAO_LONG : return self->xLong.value->size == 0 && self->xLong.value->data[0] ==0;
	case DAO_ENUM : return self->xEnum.value == 0;
	}
	return 0;
}
static daoint DString_ToInteger( DString *self )
{
	if( self->mbs ) return strtoll( self->mbs, NULL, 0 );
	return wcstoll( self->wcs, NULL, 0 );
}
double DString_ToDouble( DString *self )
{
	if( self->mbs ) return strtod( self->mbs, 0 );
	return wcstod( self->wcs, 0 );
}
daoint DaoValue_GetInteger( DaoValue *self )
{
	switch( self->type ){
	case DAO_INTEGER : return self->xInteger.value;
	case DAO_FLOAT   : return self->xFloat.value;
	case DAO_DOUBLE  : return self->xDouble.value;
	case DAO_COMPLEX : return self->xComplex.value.real;
#ifdef DAO_WITH_LONGINT
	case DAO_LONG    : return DLong_ToInteger( self->xLong.value );
#endif
	case DAO_STRING  : return DString_ToInteger( self->xString.data );
	case DAO_ENUM    : return self->xEnum.value;
	default : break;
	}
	return 0;
}
float DaoValue_GetFloat( DaoValue *self )
{
	return (float) DaoValue_GetDouble( self );
}
double DaoValue_GetDouble( DaoValue *self )
{
	DString *str;
	switch( self->type ){
	case DAO_NONE    : return 0;
	case DAO_INTEGER : return self->xInteger.value;
	case DAO_FLOAT   : return self->xFloat.value;
	case DAO_DOUBLE  : return self->xDouble.value;
	case DAO_COMPLEX : return self->xComplex.value.real;
#ifdef DAO_WITH_LONGINT
	case DAO_LONG    : return DLong_ToDouble( self->xLong.value );
#endif
	case DAO_STRING  : return DString_ToDouble( self->xString.data );
	case DAO_ENUM    : return self->xEnum.value;
	default : break;
	}
	return 0.0;
}
int DaoValue_IsNumber( DaoValue *self )
{
	if( self->type >= DAO_INTEGER && self->type <= DAO_DOUBLE ) return 1;
	return 0;
}
static void DaoValue_BasicPrint( DaoValue *self, DaoProcess *proc, DaoStream *stream, DMap *cycData )
{
	DaoType *type = DaoNamespace_GetType( proc->activeNamespace, self );
	if( self->type <= DAO_TUPLE )
		DaoStream_WriteMBS( stream, coreTypeNames[ self->type ] );
	else
		DaoStream_WriteMBS( stream, DaoValue_GetTyper( self )->name );
	if( self->type == DAO_NONE ) return;
	if( self->type == DAO_TYPE ){
		DaoStream_WriteMBS( stream, "<" );
		DaoStream_WriteMBS( stream, self->xType.name->mbs );
		DaoStream_WriteMBS( stream, ">" );
	}
	DaoStream_WriteMBS( stream, "_" );
	DaoStream_WriteInt( stream, self->type );
	DaoStream_WriteMBS( stream, "_" );
	DaoStream_WritePointer( stream, self );
	if( self->type <= DAO_TUPLE ) return;
	if( type && self == type->value ) DaoStream_WriteMBS( stream, "[default]" );
}
void DaoValue_Print( DaoValue *self, DaoProcess *proc, DaoStream *stream, DMap *cycData )
{
	DString *name;
	DaoTypeBase *typer;
	DMap *cd = cycData;
	if( self == NULL ){
		DaoStream_WriteMBS( stream, "none[0x0]" );
		return;
	}
	if( cycData == NULL ) cycData = DMap_New(0,0);
	switch( self->type ){
	case DAO_INTEGER :
		DaoStream_WriteInt( stream, self->xInteger.value ); break;
	case DAO_FLOAT   :
		DaoStream_WriteFloat( stream, self->xFloat.value ); break;
	case DAO_DOUBLE  :
		DaoStream_WriteFloat( stream, self->xDouble.value ); break;
	case DAO_COMPLEX :
		DaoStream_WriteFloat( stream, self->xComplex.value.real );
		if( self->xComplex.value.imag >= -0.0 ) DaoStream_WriteMBS( stream, "+" );
		DaoStream_WriteFloat( stream, self->xComplex.value.imag );
		DaoStream_WriteMBS( stream, "C" );
		break;
#ifdef DAO_WITH_LONGINT
	case DAO_LONG :
		name = DString_New(1);
		DLong_Print( self->xLong.value, name );
		DaoStream_WriteString( stream, name );
		DString_Delete( name );
		break;
#endif
	case DAO_ENUM  :
		name = DString_New(1);
		DaoEnum_MakeName( & self->xEnum, name );
		DaoStream_WriteMBS( stream, name->mbs );
		DaoStream_WriteMBS( stream, "(" );
		DaoStream_WriteInt( stream, self->xEnum.value );
		DaoStream_WriteMBS( stream, ")" );
		DString_Delete( name );
		break;
	case DAO_STRING  :
		DaoStream_WriteString( stream, self->xString.data ); break;
	default :
		typer = DaoVmSpace_GetTyper( self->type );
		if( typer->core->Print == DaoValue_Print ){
			DaoValue_BasicPrint( self, proc, stream, cycData );
			break;
		}
		typer->core->Print( self, proc, stream, cycData );
		break;
	}
	if( cycData != cd ) DMap_Delete( cycData );
}
complex16 DaoValue_GetComplex( DaoValue *self )
{
	complex16 com = { 0.0, 0.0 };
	switch( self->type ){
	case DAO_INTEGER : com.real = self->xInteger.value; break;
	case DAO_FLOAT   : com.real = self->xFloat.value; break;
	case DAO_DOUBLE  : com.real = self->xDouble.value; break;
	case DAO_COMPLEX : com = self->xComplex.value; break;
	default : break;
	}
	return com;
}
DLong* DaoValue_GetLong( DaoValue *self, DLong *lng )
{
#ifdef DAO_WITH_LONGINT
	switch( self->type ){
	case DAO_INTEGER : DLong_FromInteger( lng, DaoValue_GetInteger( self ) ); break;
	case DAO_FLOAT   :
	case DAO_DOUBLE  : DLong_FromDouble( lng, DaoValue_GetDouble( self ) ); break;
	case DAO_COMPLEX : DLong_FromDouble( lng, self->xComplex.value.real ); break;
	case DAO_LONG    : DLong_Move( lng, self->xLong.value ); break;
	case DAO_STRING  : DLong_FromString( lng, self->xString.data ); break;
	default : break; /* TODO list array? */
	}
#endif
	return lng;
}
DString* DaoValue_GetString( DaoValue *self, DString *str )
{
	char chs[100] = {0};
	DString_Clear( str );
	switch( self->type ){
	case DAO_INTEGER : sprintf( chs, "%" DAO_INT_FORMAT, self->xInteger.value ); break;
	case DAO_FLOAT   : sprintf( chs, "%g", self->xFloat.value ); break;
	case DAO_DOUBLE  : sprintf( chs, "%g", self->xDouble.value ); break;
	case DAO_COMPLEX : sprintf( chs, (self->xComplex.value.imag < 0) ? "%g%gC" : "%g+%gC", self->xComplex.value.real, self->xComplex.value.imag ); break;
#ifdef DAO_WITH_LONGINT
	case DAO_LONG  : DLong_Print( self->xLong.value, str ); break;
#endif
	case DAO_ENUM : DaoEnum_MakeName( & self->xEnum, str ); break;
	case DAO_STRING : DString_Assign( str, self->xString.data ); break;
	default : break;
	}
	if( self->type <= DAO_COMPLEX ) DString_SetMBS( str, chs );
	return str;
}
void DaoValue_MarkConst( DaoValue *self )
{
	DMap *map;
	DNode *it;
	daoint i, n;
	if( self == NULL || (self->xBase.trait & DAO_VALUE_CONST) ) return;
	self->xBase.trait |= DAO_VALUE_CONST;
	switch( self->type ){
	case DAO_LIST :
		for(i=0,n=self->xList.items.size; i<n; i++)
			DaoValue_MarkConst( self->xList.items.items.pValue[i] );
		break;
	case DAO_TUPLE :
		for(i=0,n=self->xTuple.size; i<n; i++)
			DaoValue_MarkConst( self->xTuple.items[i] );
		break;
	case DAO_MAP :
		map = self->xMap.items;
		for(it=DMap_First( map ); it != NULL; it = DMap_Next(map, it) ){
			DaoValue_MarkConst( it->key.pValue );
			DaoValue_MarkConst( it->value.pValue );
		}
		break;
	case DAO_OBJECT :
		n = self->xObject.defClass->instvars->size;
		for(i=1; i<n; i++) DaoValue_MarkConst( self->xObject.objValues[i] );
		if( self->xObject.parent ) DaoValue_MarkConst( self->xObject.parent );
		break;
	default : break;
	}
}

void DaoValue_Clear( DaoValue **self )
{
	DaoValue *value = *self;
	*self = NULL;
	GC_DecRC( value );
}


#ifdef DAO_WITH_NUMARRAY
static DaoArray* DaoArray_CopyX( DaoArray *self, DaoType *tp )
{
	DaoArray *copy = DaoArray_New( self->etype );
	if( tp && tp->tid == DAO_ARRAY && tp->nested->size ){
		int nt = tp->nested->items.pType[0]->tid;
		if( nt >= DAO_INTEGER && nt <= DAO_COMPLEX ) copy->etype = nt;
	}
	DaoArray_ResizeArray( copy, self->dims, self->ndim );
	DaoArray_CopyArray( copy, self );
	return copy;
}
#endif

/*
// Assumming the value "self" is compatible to the type "tp", if it is not null.
*/
DaoValue* DaoValue_SimpleCopyWithTypeX( DaoValue *self, DaoType *tp, DaoDataCache *cache )
{
	DaoEnum *e;
	daoint i, n;

	if( self == NULL ) return dao_none_value;
	if( (tp == NULL || tp->tid == self->type) && self->type < DAO_ENUM ){
		if( cache ){
			DaoValue *value = DaoDataCache_MakeValue( cache, self->type );
			switch( self->type ){
			case DAO_NONE : return self;
			case DAO_INTEGER : value->xInteger.value = self->xInteger.value; break;
			case DAO_FLOAT   : value->xFloat.value   = self->xFloat.value;   break;
			case DAO_DOUBLE  : value->xDouble.value  = self->xDouble.value;  break;
			case DAO_COMPLEX : value->xComplex.value = self->xComplex.value; break;
#ifdef DAO_WITH_LONGINT
			case DAO_LONG    : DLong_Move( value->xLong.value, self->xLong.value ); break;
#endif
			case DAO_STRING  : DString_Assign( value->xString.data, self->xString.data ); break;
			}
			return value;
		}else{
			switch( self->type ){
			case DAO_NONE : return self;
			case DAO_INTEGER : return (DaoValue*) DaoInteger_New( self->xInteger.value );
			case DAO_FLOAT   : return (DaoValue*) DaoFloat_New( self->xFloat.value );
			case DAO_DOUBLE  : return (DaoValue*) DaoDouble_New( self->xDouble.value );
			case DAO_COMPLEX : return (DaoValue*) DaoComplex_New( self->xComplex.value );
			case DAO_LONG    : return (DaoValue*) DaoLong_Copy( & self->xLong );
			case DAO_STRING  : return (DaoValue*) DaoString_Copy( & self->xString );
			}
		}
		return self; /* unreachable; */
	}else if( self->type == DAO_ENUM ){
		return (DaoValue*) DaoEnum_Copy( & self->xEnum, tp );
	}else if( tp && tp->tid >= DAO_INTEGER && tp->tid <= DAO_DOUBLE ){
		DaoValue *value = cache ? DaoDataCache_MakeValue( cache, tp->tid ) : NULL;
		switch( value == NULL ? tp->tid : 0 ){
		case DAO_INTEGER : value = (DaoValue*) DaoInteger_New(0); break;
		case DAO_FLOAT   : value = (DaoValue*) DaoFloat_New(0);   break;
		case DAO_DOUBLE  : value = (DaoValue*) DaoDouble_New(0);  break;
		}
		switch( tp->tid ){
		case DAO_INTEGER : value->xInteger.value = DaoValue_GetInteger( self ); break;
		case DAO_FLOAT   : value->xFloat.value   = DaoValue_GetFloat( self );   break;
		case DAO_DOUBLE  : value->xDouble.value  = DaoValue_GetDouble( self );  break;
		}
		return value;
	}
#ifdef DAO_WITH_NUMARRAY
	if( self->type == DAO_ARRAY && self->xArray.original ){
		DaoArray_Sliced( (DaoArray*)self );
		return self;
	}else
#endif
	if( self->type == DAO_CSTRUCT || self->type == DAO_CDATA ){
		FuncPtrSliced sliced = self->xCstruct.ctype->kernel->Sliced;
		if( sliced ) (*sliced)( self );
		return self;
	}
	if( self->xBase.trait & DAO_VALUE_NOCOPY ) return self;
	if( (self->xBase.trait & DAO_VALUE_CONST) == 0 ) return self;
	switch( self->type ){
	case DAO_LIST  : return (DaoValue*) DaoList_Copy( (DaoList*) self, tp );
	case DAO_MAP   : return (DaoValue*) DaoMap_Copy( (DaoMap*) self, tp );
	case DAO_TUPLE : return (DaoValue*) DaoTuple_Copy( (DaoTuple*) self, tp );
#ifdef DAO_WITH_NUMARRAY
	case DAO_ARRAY : return (DaoValue*) DaoArray_CopyX( (DaoArray*) self, tp );
#endif
	default : break;
	}
	return self;
}
DaoValue* DaoValue_SimpleCopyWithType( DaoValue *self, DaoType *tp )
{
	return DaoValue_SimpleCopyWithTypeX( self, tp, NULL );
}
DaoValue* DaoValue_SimpleCopy( DaoValue *self )
{
	return DaoValue_SimpleCopyWithType( self, NULL );
}

void DaoValue_CopyX( DaoValue *src, DaoValue **dest, DaoDataCache *cache )
{
	DaoValue *dest2 = *dest;
	if( src == dest2 ) return;
	if( dest2 && dest2->xBase.refCount >1 ){
		GC_DecRC( dest2 );
		*dest = dest2 = NULL;
	}
	if( dest2 == NULL ){
		src = DaoValue_SimpleCopyWithTypeX( src, NULL, cache );
		GC_IncRC( src );
		*dest = src;
		return;
	}
	if( src->type != dest2->type || src->type > DAO_ENUM ){
		src = DaoValue_SimpleCopyWithTypeX( src, NULL, cache );
		GC_ShiftRC( src, dest2 );
		*dest = src;
		return;
	}
	switch( src->type ){
	case DAO_ENUM    :
		DaoEnum_SetType( & dest2->xEnum, src->xEnum.etype );
		DaoEnum_SetValue( & dest2->xEnum, & src->xEnum, NULL );
		break;
	case DAO_INTEGER : dest2->xInteger.value = src->xInteger.value; break;
	case DAO_FLOAT   : dest2->xFloat.value = src->xFloat.value; break;
	case DAO_DOUBLE  : dest2->xDouble.value = src->xDouble.value; break;
	case DAO_COMPLEX : dest2->xComplex.value = src->xComplex.value; break;
#ifdef DAO_WITH_LONGINT
	case DAO_LONG    : DLong_Move( dest2->xLong.value, src->xLong.value ); break;
#endif
	case DAO_STRING  : DString_Assign( dest2->xString.data, src->xString.data ); break;
	}
}
void DaoValue_Copy( DaoValue *src, DaoValue **dest )
{
	DaoValue_CopyX( src, dest, NULL );
}
void DaoValue_SetType( DaoValue *to, DaoType *tp )
{
	DaoType *tp2;
	DNode *it;
	if( to->type != tp->tid && tp->tid != DAO_ANY ) return;
	if( tp->attrib & DAO_TYPE_SPEC ) return;
	switch( to->type ){
#ifdef DAO_WITH_NUMARRAY
	case DAO_ARRAY :
		if( to->xArray.size ) return;
		if( tp->tid != DAO_ARRAY || tp->nested == NULL || tp->nested->size == 0 ) break;
		tp = tp->nested->items.pType[0];
		if( tp->tid == DAO_NONE || tp->tid > DAO_COMPLEX ) break;
		DaoArray_SetNumType( (DaoArray*) to, tp->tid );
		break;
#endif
	case DAO_LIST :
		/* v : any = {}, v->unitype should be list<any> */
		if( tp->tid == DAO_ANY ) tp = dao_list_any;
		if( to->xList.unitype && !(to->xList.unitype->attrib & DAO_TYPE_UNDEF) ) break;
		GC_ShiftRC( tp, to->xList.unitype );
		to->xList.unitype = tp;
		break;
	case DAO_MAP :
		if( tp->tid == DAO_ANY ) tp = dao_map_any;
		if( to->xMap.unitype && !(to->xMap.unitype->attrib & DAO_TYPE_UNDEF) ) break;
		GC_ShiftRC( tp, to->xMap.unitype );
		to->xMap.unitype = tp;
		break;
	case DAO_TUPLE :
		tp2 = to->xTuple.unitype;
		if( tp->tid == DAO_ANY ) break;
		if( tp->nested->size ==0 ) break; /* not to the generic tuple type */
		if( tp2 == NULL || tp2->mapNames == NULL || tp2->mapNames->size ==0 ){
			GC_ShiftRC( tp, to->xTuple.unitype );
			to->xTuple.unitype = tp;
			break;
		}
		if( tp->mapNames == NULL || tp->mapNames->size ) break;
		for(it=DMap_First(tp2->mapNames); it!=NULL; it=DMap_Next(tp2->mapNames, it)){
			if( DMap_Find( tp->mapNames, it->key.pVoid ) == NULL ) break;
		}
		if( it ) break;
		GC_ShiftRC( tp, to->xTuple.unitype );
		to->xTuple.unitype = tp;
		break;
	default : break;
	}
}
static int DaoValue_TryCastTuple( DaoValue *src, DaoValue **dest, DaoType *tp )
{
	DaoTuple *tuple;
	DaoType **item_types = tp->nested->items.pType;
	DaoType *totype = src->xTuple.unitype;
	DaoValue **data = src->xTuple.items;
	DMap *names = totype ? totype->mapNames : NULL;
	DNode *node, *search;
	daoint i, T = tp->nested->size;
	int tm, eqs = 0;
	/* auto-cast tuple type, on the following conditions:
	 * (1) the item values of "dest" must match exactly to the item types of "tp";
	 * (2) "tp->mapNames" must contain "(*dest)->xTuple.unitype->mapNames"; */
	if( src->xTuple.unitype == NULL ){
		GC_IncRC( tp );
		src->xTuple.unitype = tp;
		return 1;
	}
	if( DaoType_MatchValue( tp, src, NULL ) < DAO_MT_SUB ) return 1;
	/* casting is not necessary if the tuple's field names are a superset of the
	 * field names of the target type: */
	if( tp->mapNames == NULL || tp->mapNames->size ==0 ) goto Finalize;
	if( names ){
		daoint count = 0;
		for(node=DMap_First(names); node; node=DMap_Next(names,node)){
			search = DMap_Find( tp->mapNames, node->key.pVoid );
			if( search && node->value.pInt != search->value.pInt ) return 0;
			count += search != NULL;
		}
		/* be superset of the field names of the target type: */
		if( count == tp->mapNames->size ) goto Finalize;
	}
Finalize:
	tuple = DaoTuple_New( T );
	for(i=0; i<T; i++){
		DaoType *it = item_types[i];
		if( it->tid == DAO_PAR_NAMED ) it = & it->aux->xType;
		DaoValue_Move( data[i], tuple->items+i, it );
	}
	GC_IncRC( tp );
	tuple->unitype = tp;
	GC_ShiftRC( tuple, *dest );
	*dest = (DaoValue*) tuple;
	return 1;
}
static int DaoValue_MoveVariant( DaoValue *src, DaoValue **dest, DaoType *tp )
{
	DaoType *itp = NULL;
	daoint j, k, n, mt = 0;
	for(j=0,n=tp->nested->size; j<n; j++){
		k = DaoType_MatchValue( tp->nested->items.pType[j], src, NULL );
		if( k > mt ){
			itp = tp->nested->items.pType[j];
			mt = k;
			if( mt == DAO_MT_EQ ) break;
		}
	}
	if( itp == NULL ) return 0;
	return DaoValue_Move( src, dest, itp );
}
int DaoValue_Move4( DaoValue *S, DaoValue **D, DaoType *T, DMap *defs, DaoDataCache *cache )
{
	int tm = 1;
	switch( (T->tid << 8) | S->type ){
	case (DAO_INTEGER << 8) | DAO_INTEGER :
	case (DAO_INTEGER << 8) | DAO_FLOAT   :
	case (DAO_INTEGER << 8) | DAO_DOUBLE  :
	case (DAO_FLOAT   << 8) | DAO_INTEGER :
	case (DAO_FLOAT   << 8) | DAO_FLOAT   :
	case (DAO_FLOAT   << 8) | DAO_DOUBLE  :
	case (DAO_DOUBLE  << 8) | DAO_INTEGER :
	case (DAO_DOUBLE  << 8) | DAO_FLOAT   :
	case (DAO_DOUBLE  << 8) | DAO_DOUBLE  :
	case (DAO_COMPLEX << 8) | DAO_COMPLEX :
	case (DAO_LONG    << 8) | DAO_LONG    :
	case (DAO_STRING  << 8) | DAO_STRING  :
		S = DaoValue_SimpleCopyWithTypeX( S, T, cache );
		GC_ShiftRC( S, *D );
		*D = S;
		return 1;
	}
	if( !(S->xTuple.trait & DAO_VALUE_CONST) ){
		DaoType *ST = NULL;
		switch( (S->type << 8) | T->tid ){
		case (DAO_TUPLE<<8)|DAO_TUPLE : ST = S->xTuple.unitype; break;
		case (DAO_ARRAY<<8)|DAO_ARRAY : ST = dao_array_types[ S->xArray.etype ]; break;
		case (DAO_LIST <<8)|DAO_LIST  : ST = S->xList.unitype; break;
		case (DAO_MAP  <<8)|DAO_MAP   : ST = S->xMap.unitype; break;
		case (DAO_CDATA<<8)|DAO_CDATA : ST = S->xCdata.ctype; break;
		case (DAO_CSTRUCT<<8)|DAO_CSTRUCT : ST = S->xCstruct.ctype; break;
		case (DAO_OBJECT<<8)|DAO_OBJECT : ST = S->xObject.defClass->objType; break;
		}
		if( ST == T ){
			DaoValue *D2 = *D;
			GC_ShiftRC( S, D2 );
			*D = S;
			return 1;
		}
	}
	if( (T->tid == DAO_OBJECT || T->tid == DAO_CDATA || T->tid == DAO_CSTRUCT) && S->type == DAO_OBJECT ){
		if( S->xObject.defClass != & T->aux->xClass ){
			S = DaoObject_CastToBase( S->xObject.rootObject, T );
			tm = (S != NULL);
		}
	}else if( (T->tid == DAO_CLASS || T->tid == DAO_CTYPE) && S->type == DAO_CLASS ){
		if( S->xClass.clsType != T && T->aux != NULL ){ /* T->aux == NULL for "class"; */
			S = DaoClass_CastToBase( (DaoClass*)S, T );
			tm = (S != NULL);
		}
	}else if( T->tid == DAO_CTYPE && S->type == DAO_CTYPE ){
		if( S->xCtype.ctype != T ){
			S = DaoType_CastToParent( S, T );
			tm = (S != NULL);
		}
	}else if( T->tid == DAO_ROUTINE && T->overloads == 0 && S->type == DAO_ROUTINE && S->xRoutine.overloads ){
		DArray *routines = S->xRoutine.overloads->routines;
		int i, k, n;
		/*
		// Do not use DaoRoutine_ResolveByType( S, ... )
		// "S" should match to "T", not the other way around!
		*/
		tm = 0;
		for(i=0,n=routines->size; i<n; i++){
			DaoRoutine *rout = routines->items.pRoutine[i];
			k = rout->routType == T ? DAO_MT_EQ : DaoType_MatchTo( rout->routType, T, defs );
			if( k > tm ) tm = k;
			if( rout->routType == T ){
				S = (DaoValue*) rout;
				break;
			}
		}
	}else{
		tm = DaoType_MatchValue( T, S, defs );
	}
#if 0
	if( tm ==0 ){
		printf( "T = %p; S = %p, type = %i %i\n", T, S, S->type, DAO_ROUTINE );
		printf( "T: %s %i %i\n", T->name->mbs, T->tid, tm );
		if( S->type == DAO_LIST ) printf( "%s\n", S->xList.unitype->name->mbs );
		if( S->type == DAO_TUPLE ) printf( "%p\n", S->xTuple.unitype );
	}
	printf( "S->type = %p %s %i\n", S, T->name->mbs, tm );
#endif
	if( tm == 0 ) return 0;
	/* composite known types must match exactly. example,
	 * where it will not work if composite types are allowed to match loosely.
	 * d : list<list<int>> = {};
	 * e : list<float> = { 1.0 };
	 * d.append( e );
	 *
	 * but if d is of type list<list<any>>,
	 * the matching do not necessary to be exact.
	 */
	S = DaoValue_SimpleCopyWithTypeX( S, T, cache );
	GC_ShiftRC( S, *D );
	*D = S;
	if( S->type == DAO_TUPLE && S->xTuple.unitype != T && tm >= DAO_MT_SIM ){
		return DaoValue_TryCastTuple( S, D, T );
	}else if( T && T->tid == S->type && !(T->attrib & DAO_TYPE_SPEC) ){
		DaoValue_SetType( S, T );
	}
	return 1;
}
int DaoValue_Move5( DaoValue *S, DaoValue **D, DaoType *T, DMap *defs, DaoDataCache *cache )
{
	DaoValue *D2 = *D;
	if( S == D2 ) return 1;
	if( S == NULL ){
		GC_DecRC( *D );
		*D = NULL;
		return 0;
	}
	if( T == NULL ){
		DaoValue_CopyX( S, D, cache );
		return 1;
	}
	switch( T->tid ){
	case DAO_UDT :
		DaoValue_CopyX( S, D, cache );
		return 1;
	case DAO_THT :
		if( T->aux ) return DaoValue_Move4( S, D, (DaoType*) T->aux, defs, cache );
		DaoValue_CopyX( S, D, cache );
		return 1;
	case DAO_ANY :
		DaoValue_CopyX( S, D, cache );
		DaoValue_SetType( *D, T );
		return 1;
	case DAO_VALTYPE :
		if( DaoValue_Compare( S, T->aux ) !=0 ) return 0;
		DaoValue_CopyX( S, D, cache );
		return 1;
	case DAO_VARIANT :
		return DaoValue_MoveVariant( S, D, T );
	default : break;
	}
	if( D2 && D2->xBase.refCount >1 ){
		GC_DecRC( D2 );
		*D = D2 = NULL;
	}
	if( D2 == NULL || D2->type != T->tid ) return DaoValue_Move4( S, D, T, defs, cache );

	switch( (S->type << 8) | T->tid ){
	case (DAO_STRING<<8)|DAO_STRING :
		DString_Assign( D2->xString.data, S->xString.data );
		break;
	case (DAO_ENUM<<8)|DAO_ENUM :
		DaoEnum_SetType( & D2->xEnum, T );
		DaoEnum_SetValue( & D2->xEnum, & S->xEnum, NULL );
		break;
	case (DAO_INTEGER<<8)|DAO_INTEGER : D2->xInteger.value = S->xInteger.value; break;
	case (DAO_INTEGER<<8)|DAO_FLOAT   : D2->xFloat.value   = S->xInteger.value; break;
	case (DAO_INTEGER<<8)|DAO_DOUBLE  : D2->xDouble.value  = S->xInteger.value; break;
	case (DAO_FLOAT  <<8)|DAO_INTEGER : D2->xInteger.value = S->xFloat.value; break;
	case (DAO_FLOAT  <<8)|DAO_FLOAT   : D2->xFloat.value   = S->xFloat.value; break;
	case (DAO_FLOAT  <<8)|DAO_DOUBLE  : D2->xDouble.value  = S->xFloat.value; break;
	case (DAO_DOUBLE <<8)|DAO_INTEGER : D2->xInteger.value = S->xDouble.value; break;
	case (DAO_DOUBLE <<8)|DAO_FLOAT   : D2->xFloat.value   = S->xDouble.value; break;
	case (DAO_DOUBLE <<8)|DAO_DOUBLE  : D2->xDouble.value  = S->xDouble.value; break;
	case (DAO_COMPLEX<<8)|DAO_COMPLEX : D2->xComplex.value = S->xComplex.value; break;
#ifdef DAO_WITH_LONGINT
	case (DAO_LONG   <<8)|DAO_LONG    : DLong_Move( D2->xLong.value, S->xLong.value ); break;
#endif
	default : return DaoValue_Move4( S, D, T, defs, cache );
	}
	return 1;
}
int DaoValue_MoveX( DaoValue *S, DaoValue **D, DaoType *T, DaoDataCache *cache )
{
	return DaoValue_Move5( S, D, T, NULL, cache );
}
int DaoValue_Move( DaoValue *S, DaoValue **D, DaoType *T )
{
	return DaoValue_Move5( S, D, T, NULL, NULL );
}
int DaoValue_Move2( DaoValue *S, DaoValue **D, DaoType *T, DMap *defs, DaoDataCache *cache )
{
	int rc = DaoValue_Move5( S, D, T, defs, cache );
	if( rc == 0 || T == NULL ) return rc;
	if( S->type <= DAO_TUPLE || S->type != T->tid ) return rc;
	if( S->type == DAO_CDATA ){
		if( S->xCdata.data == NULL ) rc = 0;
	}else{
		if( S == T->value ) rc = 0;
	}
	return rc;
}


int DaoValue_Type( DaoValue *self )
{
	return self->type;
}

DaoInteger* DaoValue_CastInteger( DaoValue *self )
{
	if( self == NULL || self->type != DAO_INTEGER ) return NULL;
	return (DaoInteger*) self;
}
DaoFloat* DaoValue_CastFloat( DaoValue *self )
{
	if( self == NULL || self->type != DAO_FLOAT ) return NULL;
	return (DaoFloat*) self;
}
DaoDouble* DaoValue_CastDouble( DaoValue *self )
{
	if( self == NULL || self->type != DAO_DOUBLE ) return NULL;
	return (DaoDouble*) self;
}
DaoComplex* DaoValue_CastComplex( DaoValue *self )
{
	if( self == NULL || self->type != DAO_COMPLEX ) return NULL;
	return (DaoComplex*) self;
}
DaoLong* DaoValue_CastLong( DaoValue *self )
{
	if( self == NULL || self->type != DAO_LONG ) return NULL;
	return (DaoLong*) self;
}
DaoString* DaoValue_CastString( DaoValue *self )
{
	if( self == NULL || self->type != DAO_STRING ) return NULL;
	return (DaoString*) self;
}
DaoEnum* DaoValue_CastEnum( DaoValue *self )
{
	if( self == NULL || self->type != DAO_ENUM ) return NULL;
	return (DaoEnum*) self;
}
DaoArray* DaoValue_CastArray( DaoValue *self )
{
	if( self == NULL || self->type != DAO_ARRAY ) return NULL;
	return (DaoArray*) self;
}
DaoList* DaoValue_CastList( DaoValue *self )
{
	if( self == NULL || self->type != DAO_LIST ) return NULL;
	return (DaoList*) self;
}
DaoMap* DaoValue_CastMap( DaoValue *self )
{
	if( self == NULL || self->type != DAO_MAP ) return NULL;
	return (DaoMap*) self;
}
DaoTuple* DaoValue_CastTuple( DaoValue *self )
{
	if( self == NULL || self->type != DAO_TUPLE ) return NULL;
	return (DaoTuple*) self;
}
DaoStream* DaoValue_CastStream( DaoValue *self )
{
	if( self == NULL || self->type != DAO_CSTRUCT ) return NULL;
	return (DaoStream*) DaoValue_CastCstruct( self, dao_type_stream );
}
DaoObject* DaoValue_CastObject( DaoValue *self )
{
	if( self == NULL || self->type != DAO_OBJECT ) return NULL;
	return (DaoObject*) self;
}
DaoCstruct* DaoValue_CastCstruct( DaoValue *self, DaoType *type )
{
	if( self == NULL || type == NULL ) return (DaoCstruct*) self;
	if( self->type != DAO_CSTRUCT && self->type != DAO_CDATA ) return NULL;
	if( DaoType_ChildOf( self->xCstruct.ctype, type ) ) return (DaoCstruct*) self;
	return NULL;
}
DaoCdata* DaoValue_CastCdata( DaoValue *self, DaoType *type )
{
	if( self == NULL || type == NULL ) return (DaoCdata*) self;
	if( self->type != DAO_CDATA ) return NULL;
	if( DaoType_ChildOf( self->xCdata.ctype, type ) ) return (DaoCdata*) self;
	return NULL;
}
DaoClass* DaoValue_CastClass( DaoValue *self )
{
	if( self == NULL || self->type != DAO_CLASS ) return NULL;
	return (DaoClass*) self;
}
DaoInterface* DaoValue_CastInterface( DaoValue *self )
{
	if( self == NULL || self->type != DAO_INTERFACE ) return NULL;
	return (DaoInterface*) self;
}
DaoRoutine* DaoValue_CastRoutine( DaoValue *self )
{
	if( self == NULL || self->type != DAO_ROUTINE ) return NULL;
	return (DaoRoutine*) self;
}
DaoProcess* DaoValue_CastProcess( DaoValue *self )
{
	if( self == NULL || self->type != DAO_PROCESS ) return NULL;
	return (DaoProcess*) self;
}
DaoNamespace* DaoValue_CastNamespace( DaoValue *self )
{
	if( self == NULL || self->type != DAO_NAMESPACE ) return NULL;
	return (DaoNamespace*) self;
}
DaoType* DaoValue_CastType( DaoValue *self )
{
	if( self == NULL || self->type != DAO_TYPE ) return NULL;
	return (DaoType*) self;
}

DaoValue* DaoValue_MakeNone()
{
	return dao_none_value;
}

daoint DaoValue_TryGetInteger( DaoValue *self )
{
	if( self->type != DAO_INTEGER ) return 0;
	return self->xInteger.value;
}
float DaoValue_TryGetFloat( DaoValue *self )
{
	if( self->type != DAO_FLOAT ) return 0.0;
	return self->xFloat.value;
}
double DaoValue_TryGetDouble( DaoValue *self )
{
	if( self->type != DAO_DOUBLE ) return 0.0;
	return self->xDouble.value;
}
int DaoValue_TryGetEnum(DaoValue *self)
{
	if( self->type != DAO_ENUM ) return 0;
	return self->xEnum.value;
}
complex16 DaoValue_TryGetComplex( DaoValue *self )
{
	complex16 com = {0.0,0.0};
	if( self->type != DAO_COMPLEX ) return com;
	return self->xComplex.value;
}
char* DaoValue_TryGetMBString( DaoValue *self )
{
	if( self->type != DAO_STRING ) return NULL;
	return DString_GetMBS( self->xString.data );
}
wchar_t* DaoValue_TryGetWCString( DaoValue *self )
{
	if( self->type != DAO_STRING ) return NULL;
	return DString_GetWCS( self->xString.data );
}
DString* DaoValue_TryGetString( DaoValue *self )
{
	if( self->type != DAO_STRING ) return NULL;
	return self->xString.data;
}
void* DaoValue_TryGetArray( DaoValue *self )
{
	if( self->type != DAO_ARRAY ) return NULL;
	return self->xArray.data.p;
}
void* DaoValue_TryCastCdata( DaoValue *self, DaoType *type )
{
	if( self->type != DAO_CDATA ) return NULL;
	return DaoCdata_CastData( & self->xCdata, type );
}
void* DaoValue_TryGetCdata( DaoValue *self )
{
	if( self->type != DAO_CDATA ) return NULL;
	return self->xCdata.data;
}
void** DaoValue_TryGetCdata2( DaoValue *self )
{
	if( self->type != DAO_CDATA ) return NULL;
	return & self->xCdata.data;
}
void DaoValue_ClearAll( DaoValue *v[], int n )
{
	int i;
	for(i=0; i<n; i++) DaoValue_Clear( v + i );
}

void DaoProcess_CacheValue( DaoProcess *self, DaoValue *value )
{
	DArray_Append( self->factory, NULL );
	GC_IncRC( value );
	self->factory->items.pValue[ self->factory->size - 1 ] = value;
}
void DaoProcess_PopValues( DaoProcess *self, int N )
{
	if( N < 0 ) return;
	if( N >= (int)self->factory->size ){
		DArray_Clear( self->factory );
	}else{
		DArray_Erase( self->factory, self->factory->size - N, N );
	}
}
DaoValue** DaoProcess_GetLastValues( DaoProcess *self, int N )
{
	if( N > (int)self->factory->size ) return self->factory->items.pValue;
	return self->factory->items.pValue + (self->factory->size - N);
}

DaoNone* DaoProcess_NewNone( DaoProcess *self )
{
	DaoNone *res = DaoNone_New();
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoInteger* DaoProcess_NewInteger( DaoProcess *self, daoint v )
{
	DaoInteger *res = DaoInteger_New( v );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoFloat* DaoProcess_NewFloat( DaoProcess *self, float v )
{
	DaoFloat *res = DaoFloat_New( v );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoDouble* DaoProcess_NewDouble( DaoProcess *self, double v )
{
	DaoDouble *res = DaoDouble_New( v );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoComplex* DaoProcess_NewComplex( DaoProcess *self, complex16 v )
{
	DaoComplex *res = DaoComplex_New( v );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoLong* DaoProcess_NewLong( DaoProcess *self )
{
	DaoLong *res = DaoLong_New();
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoString* DaoProcess_NewString( DaoProcess *self, int mbs )
{
	DaoString *res = DaoString_New( mbs );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoString* DaoProcess_NewMBString( DaoProcess *self, const char *s, daoint n )
{
	DaoString *res = DaoString_New(1);
	if( s ) DString_SetDataMBS( res->data, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoString* DaoProcess_NewWCString( DaoProcess *self, const wchar_t *s, daoint n )
{
	DaoString *res = DaoString_New(0);
	if( s ) DString_SetDataWCS( res->data, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoEnum* DaoProcess_NewEnum( DaoProcess *self, DaoType *type, int value )
{
	DaoEnum *res = DaoEnum_New( type, value );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoTuple* DaoProcess_NewTuple( DaoProcess *self, int count )
{
	int i, N = abs( count );
	int M = self->factory->size;
	DaoValue **values = self->factory->items.pValue;
	DaoTuple *res = NULL;
	if( count < 0 ){
		if( M < N ) return NULL;
		res = DaoTuple_New( N );
		for(i=0; i<N; i++) DaoTuple_SetItem( res, values[M-N+i], i );
	}
	if( res == NULL ) res = DaoTuple_New( N );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoList* DaoProcess_NewList( DaoProcess *self )
{
	DaoList *res = DaoList_New();
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoMap* DaoProcess_NewMap( DaoProcess *self, unsigned int hashing )
{
	DaoMap *res = DaoMap_New( hashing );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
#ifdef DAO_WITH_NUMARRAY
DaoArray* DaoProcess_NewArray( DaoProcess *self, int type )
{
	DaoArray *res = DaoArray_New( type );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewVectorSB( DaoProcess *self, signed char *s, daoint n )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetVectorSB( res, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewVectorUB( DaoProcess *self, unsigned char *s, daoint n )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetVectorUB( res, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewVectorSS( DaoProcess *self, signed short *s, daoint n )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetVectorSS( res, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewVectorUS( DaoProcess *self, unsigned short *s, daoint n )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetVectorUS( res, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewVectorSI( DaoProcess *self, signed int *s, daoint n )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetVectorSI( res, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewVectorUI( DaoProcess *self, unsigned int *s, daoint n )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetVectorUI( res, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewVectorI( DaoProcess *self, daoint *s, daoint n )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetVectorI( res, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewVectorF( DaoProcess *self, float *s, daoint n )
{
	DaoArray *res = DaoArray_New( DAO_FLOAT );
	if( s ) DaoArray_SetVectorF( res, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewVectorD( DaoProcess *self, double *s, daoint n )
{
	DaoArray *res = DaoArray_New( DAO_DOUBLE );
	if( s ) DaoArray_SetVectorD( res, s, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewMatrixSB( DaoProcess *self, signed char **s, daoint n, daoint m )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetMatrixSB( res, s, n, m );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewMatrixUB( DaoProcess *self, unsigned char **s, daoint n, daoint m )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetMatrixUB( res, s, n, m );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewMatrixSS( DaoProcess *self, signed short **s, daoint n, daoint m )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetMatrixSS( res, s, n, m );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewMatrixUS( DaoProcess *self, unsigned short **s, daoint n, daoint m )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetMatrixUS( res, s, n, m );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewMatrixSI( DaoProcess *self, signed int **s, daoint n, daoint m )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetMatrixSI( res, s, n, m );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewMatrixUI( DaoProcess *self, unsigned int **s, daoint n, daoint m )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetMatrixUI( res, s, n, m );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewMatrixI( DaoProcess *self, daoint **s, daoint n, daoint m )
{
	DaoArray *res = DaoArray_New( DAO_INTEGER );
	if( s ) DaoArray_SetMatrixI( res, s, n, m );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewMatrixF( DaoProcess *self, float **s, daoint n, daoint m )
{
	DaoArray *res = DaoArray_New( DAO_FLOAT );
	if( s ) DaoArray_SetMatrixF( res, s, n, m );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewMatrixD( DaoProcess *self, double **s, daoint n, daoint m )
{
	DaoArray *res = DaoArray_New( DAO_DOUBLE );
	if( s ) DaoArray_SetMatrixD( res, s, n, m );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoArray* DaoProcess_NewBuffer( DaoProcess *self, void *p, daoint n )
{
	DaoArray *res = DaoArray_New(0);
	DaoArray_SetBuffer( res, p, n );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
#else
static DaoArray* DaoValue_NewArray()
{
	printf( "Error: numeric array is disabled!\n" );
	return NULL;
}
DaoArray* DaoProcess_NewVectorB( DaoProcess *self, char *s, daoint n )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewVectorUB( DaoProcess *self, unsigned char *s, daoint n )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewVectorS( DaoProcess *self, short *s, daoint n )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewVectorUS( DaoProcess *self, unsigned short *s, daoint n )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewVectorI( DaoProcess *self, daoint *s, daoint n )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewVectorUI( DaoProcess *self, unsigned int *s, daoint n )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewVectorF( DaoProcess *self, float *s, daoint n )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewVectorD( DaoProcess *self, double *s, daoint n )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewMatrixB( DaoProcess *self, signed char **s, daoint n, daoint m )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewMatrixUB( DaoProcess *self, unsigned char **s, daoint n, daoint m )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewMatrixS( DaoProcess *self, short **s, daoint n, daoint m )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewMatrixUS( DaoProcess *self, unsigned short **s, daoint n, daoint m )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewMatrixI( DaoProcess *self, daoint **s, daoint n, daoint m )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewMatrixUI( DaoProcess *self, unsigned int **s, daoint n, daoint m )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewMatrixF( DaoProcess *self, float **s, daoint n, daoint m )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewMatrixD( DaoProcess *self, double **s, daoint n, daoint m )
{
	return DaoValue_NewArray();
}
DaoArray* DaoProcess_NewBuffer( DaoProcess *self, void *s, daoint n )
{
	return DaoValue_NewArray();
}
#endif
DaoStream* DaoProcess_NewStream( DaoProcess *self, FILE *f )
{
	DaoStream *res = DaoStream_New();
	DaoStream_SetFile( res, f );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
DaoCdata* DaoProcess_NewCdata( DaoProcess *self, DaoType *type, void *data, int owned )
{
	DaoCdata *res = owned ? DaoCdata_New( type, data ) : DaoCdata_Wrap( type, data );
	DaoProcess_CacheValue( self, (DaoValue*) res );
	return res;
}
