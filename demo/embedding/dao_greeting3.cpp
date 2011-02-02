#include"dao_greeting.h"

void Dao_Get_Object_Method( DaoCData *cd, DaoObject **ob, DaoRoutine **ro, const char *name )
{
  DValue va;
  if( cd == NULL ) return;
  *ob = DaoCData_GetObject( cd );
  if( *ob == NULL ) return;
  va = DaoObject_GetField( *ob, name );
  if( va.t == DAO_ROUTINE ) *ro = va.v.routine;
}

static otto Function_10007( DaoRoutine *_ro, DaoObject *_ob, const otto &value )
{
  const DValue _dao_nil = {0,0,0,0,{0}};
  DValue _dp[1] = { _dao_nil };
  DValue *_dp2[1] = { _dp+0 };
  DValue _res;
  DaoCData *_cd;
  DaoVmProcess *_vmp;
  otto _test;
  if( _ro == NULL ) goto EndCall;
  _dp[0] = DValue_WrapCData( dao_otto_Typer, (void*) & value );

  _vmp = DaoVmSpace_AcquireProcess( __daoVmSpace );
  if( DaoVmProcess_Call( _vmp, _ro, _ob, _dp2, 1 ) ==0 ) goto EndCall;
  _res = DaoVmProcess_GetReturned( _vmp );
  DaoVmSpace_ReleaseProcess( __daoVmSpace, _vmp );
  if( _res.t == DAO_OBJECT && (_cd = DaoObject_MapCData( _res.v.object, dao_otto_Typer ) ) ){
    _res.t = DAO_CDATA;
    _res.v.cdata = _cd;
  }
  if( _res.t == DAO_CDATA && DaoCData_IsType( _res.v.cdata, dao_otto_Typer ) ){
    _test = *(otto*) DaoCData_CastData( _res.v.cdata, dao_otto_Typer );
  }

EndCall:
  DValue_ClearAll( _dp, 1 );
  return _test;
}

static void Function_10005( DaoRoutine *_ro, DaoObject *_ob, const Greeting &g )
{
  const DValue _dao_nil = {0,0,0,0,{0}};
  DValue _dp[1] = { _dao_nil };
  DValue *_dp2[1] = { _dp+0 };
  if( _ro == NULL ) return;
  _dp[0] = DValue_WrapCData( dao_Greeting_Typer, (void*) & g );

  DaoVmProcess *_vmp = DaoVmSpace_AcquireProcess( __daoVmSpace );
  DaoVmProcess_Call( _vmp, _ro, _ob, _dp2, 1 );
  DaoVmSpace_ReleaseProcess( __daoVmSpace, _vmp );
  DValue_ClearAll( _dp, 1 );
}

static void Function_10002( DaoRoutine *_ro, DaoObject *_ob, const char* msg )
{
  const DValue _dao_nil = {0,0,0,0,{0}};
  DValue _dp[1] = { _dao_nil };
  DValue *_dp2[1] = { _dp+0 };
  if( _ro == NULL ) return;
  _dp[0] = DValue_NewMBString( (char*) msg, strlen( (char*)msg ) );

  DaoVmProcess *_vmp = DaoVmSpace_AcquireProcess( __daoVmSpace );
  DaoVmProcess_Call( _vmp, _ro, _ob, _dp2, 1 );
  DaoVmSpace_ReleaseProcess( __daoVmSpace, _vmp );
  DValue_ClearAll( _dp, 1 );
}

AutobindTest* Dao_AutobindTest_New()
{
	AutobindTest *self = new AutobindTest();
	return self;
}



DaoCxx_Greeting* DAO_DLL_GREETING DaoCxx_Greeting_New( const char* msg )
{
	DaoCxx_Greeting *self = new DaoCxx_Greeting( msg );
	self->DaoInitWrapper();
	return self;
}
void DaoCxxVirt_Greeting::DaoInitWrapper( Greeting *s, DaoCData *d )
{
	self = s;
	cdata = d;

}
DaoCxx_Greeting::~DaoCxx_Greeting()
{
	if( cdata ){
		DaoCData_SetData( cdata, NULL );
		DaoCData_SetExtReference( cdata, 0 );
	} 
}
void DaoCxx_Greeting::DaoInitWrapper()
{
	cdata = DaoCData_New( dao_Greeting_Typer, this );
	DaoCxxVirt_Greeting::DaoInitWrapper( this, cdata );
}
Greeting* DAO_DLL_GREETING Dao_Greeting_Copy( const Greeting &p )
{
	Greeting *object = new Greeting( p );
	return object;
}
void DaoCxxVirt_Greeting::DoGreeting( const char* name )
{
  DaoObject *_ob = NULL;
  DaoRoutine *_ro = NULL;
  Dao_Get_Object_Method( cdata, & _ob, & _ro, "DoGreeting" );
  if( _ro ==NULL || _ob ==NULL ) return;
  Function_10002( _ro, _ob, name );
}
void DaoCxxVirt_Greeting::VirtWithDefault( const Greeting &g )
{
  DaoObject *_ob = NULL;
  DaoRoutine *_ro = NULL;
  Dao_Get_Object_Method( cdata, & _ob, & _ro, "VirtWithDefault" );
  if( _ro ==NULL || _ob ==NULL ) return;
  Function_10005( _ro, _ob, g );
}
void DaoCxx_Greeting::DoGreeting( const char* name )
{
  DaoObject *_o = DaoCData_GetObject( cdata );
  if( _o && DaoObject_GetField( _o, "DoGreeting" ).t ==DAO_ROUTINE ){
     DaoCxxVirt_Greeting::DoGreeting( name );
  }else{
     Greeting::DoGreeting( name );
  }
}
void DaoCxx_Greeting::VirtWithDefault( const Greeting &g )
{
  DaoObject *_o = DaoCData_GetObject( cdata );
  if( _o && DaoObject_GetField( _o, "VirtWithDefault" ).t ==DAO_ROUTINE ){
     DaoCxxVirt_Greeting::VirtWithDefault( g );
  }else{
     Greeting::VirtWithDefault( g );
  }
}


DaoCxx_Greeting2* DAO_DLL_GREETING DaoCxx_Greeting2_New(  )
{
	DaoCxx_Greeting2 *self = new DaoCxx_Greeting2(  );
	self->DaoInitWrapper();
	return self;
}
void DaoCxxVirt_Greeting2::DaoInitWrapper( Greeting2 *s, DaoCData *d )
{
	self = s;
	cdata = d;
	DaoCxxVirt_Greeting::DaoInitWrapper( s, d );
}
DaoCxx_Greeting2::~DaoCxx_Greeting2()
{
	if( cdata ){
		DaoCData_SetData( cdata, NULL );
		DaoCData_SetExtReference( cdata, 0 );
	} 
}
void DaoCxx_Greeting2::DaoInitWrapper()
{
	cdata = DaoCData_New( dao_Greeting2_Typer, this );
	DaoCxxVirt_Greeting2::DaoInitWrapper( this, cdata );
}
Greeting2* DAO_DLL_GREETING Dao_Greeting2_Copy( const Greeting2 &p )
{
	Greeting2 *object = new Greeting2( p );
	return object;
}
void DaoCxxVirt_Greeting2::DoGreeting( const char* name )
{
   DaoCxxVirt_Greeting::DoGreeting( name );
}
void DaoCxxVirt_Greeting2::VirtWithDefault( const Greeting &g )
{
   DaoCxxVirt_Greeting::VirtWithDefault( g );
}
void DaoCxx_Greeting2::DoGreeting( const char* name )
{
  DaoObject *_o = DaoCData_GetObject( cdata );
  if( _o && DaoObject_GetField( _o, "DoGreeting" ).t ==DAO_ROUTINE ){
     DaoCxxVirt_Greeting2::DoGreeting( name );
  }else{
     Greeting::DoGreeting( name );
  }
}
void DaoCxx_Greeting2::VirtWithDefault( const Greeting &g )
{
  DaoObject *_o = DaoCData_GetObject( cdata );
  if( _o && DaoObject_GetField( _o, "VirtWithDefault" ).t ==DAO_ROUTINE ){
     DaoCxxVirt_Greeting2::VirtWithDefault( g );
  }else{
     Greeting::VirtWithDefault( g );
  }
}


namespace CxxNS{
Test* Dao_Test_New()
{
	Test *self = new Test();
	return self;
}

}

DaoCxx_otto* DAO_DLL_GREETING DaoCxx_otto_New( int b )
{
	DaoCxx_otto *self = new DaoCxx_otto( b );
	self->DaoInitWrapper();
	return self;
}
void DaoCxxVirt_otto::DaoInitWrapper( otto *s, DaoCData *d )
{
	self = s;
	cdata = d;

}
DaoCxx_otto::~DaoCxx_otto()
{
	if( cdata ){
		DaoCData_SetData( cdata, NULL );
		DaoCData_SetExtReference( cdata, 0 );
	} 
}
void DaoCxx_otto::DaoInitWrapper()
{
	cdata = DaoCData_New( dao_otto_Typer, this );
	DaoCxxVirt_otto::DaoInitWrapper( this, cdata );
}
otto* DAO_DLL_GREETING Dao_otto_Copy( const otto &p )
{
	otto *object = new otto( p );
	return object;
}
otto DaoCxxVirt_otto::test( const otto &value )
{
  DaoObject *_ob = NULL;
  DaoRoutine *_ro = NULL;
  otto _test;
  Dao_Get_Object_Method( cdata, & _ob, & _ro, "test" );
  if( _ro ==NULL || _ob ==NULL ) return _test;
  return (otto)Function_10007( _ro, _ob, value );
}
otto DaoCxx_otto::test( const otto &value )
{
  DaoObject *_o = DaoCData_GetObject( cdata );
  if( _o && DaoObject_GetField( _o, "test" ).t ==DAO_ROUTINE ){
    return DaoCxxVirt_otto::test( value );
  }else{
    return otto::test( value );
  }
}


DaoCxx_otto2* DAO_DLL_GREETING DaoCxx_otto2_New(  )
{
	DaoCxx_otto2 *self = new DaoCxx_otto2(  );
	self->DaoInitWrapper();
	return self;
}
void DaoCxxVirt_otto2::DaoInitWrapper( otto2 *s, DaoCData *d )
{
	self = s;
	cdata = d;
	DaoCxxVirt_otto::DaoInitWrapper( s, d );
}
DaoCxx_otto2::~DaoCxx_otto2()
{
	if( cdata ){
		DaoCData_SetData( cdata, NULL );
		DaoCData_SetExtReference( cdata, 0 );
	} 
}
void DaoCxx_otto2::DaoInitWrapper()
{
	cdata = DaoCData_New( dao_otto2_Typer, this );
	DaoCxxVirt_otto2::DaoInitWrapper( this, cdata );
}
otto2* DAO_DLL_GREETING Dao_otto2_Copy( const otto2 &p )
{
	otto2 *object = new otto2( p );
	return object;
}
otto DaoCxxVirt_otto2::test( const otto &value )
{
  return DaoCxxVirt_otto::test( value );
}
otto DaoCxx_otto2::test( const otto &value )
{
  DaoObject *_o = DaoCData_GetObject( cdata );
  if( _o && DaoObject_GetField( _o, "test" ).t ==DAO_ROUTINE ){
    return DaoCxxVirt_otto2::test( value );
  }else{
    return otto::test( value );
  }
}
