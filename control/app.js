

const projectcontrol = require( "./projectcontrol/index.js" );

projectcontrol.onhangup = ( call ) =>
{
  console.log( "global hangup" )
}

projectcontrol.onnewcall = ( call ) =>
{
  if( call.originator ) return;

  console.log( "new call inbound call" );
  if( call.haserror ) console.log( call.error );

  if( "3" == call.destination )
  {
    call.newcall( "1003" );
    return;
  }


  call.onhangup = () =>
  {
    console.log( "hung up" );
  }

  call.onanswer = () =>
  {
    console.log( "Answered" );
    setTimeout( () => { call.hangup(); }, 2000 );
  }

  call.ring();
  setTimeout( () => { call.answer(); }, 2000 );
}

if(1) setTimeout( () =>
{

  var callobj = {
    from: {
      user: "1003",
      domain: "bling.babblevoice.com"
    },
    to: {
      user: "1003",
      domain: "sipgw.magrathea.net"
    },
    cid: {
      number: "1003",
      name: "Nick Knight"
    }
  };

  var call = projectcontrol.newcall( callobj )
  call.onnewcall = ( call ) =>
  {
    if( call.haserror )
    {
      console.log( call.error )
      return;
    }

    call.onringing = () =>
    {
      console.log( "Call is ringing Yaya" )
    }

    call.onanswer = () =>
    {
      console.log( "outbound call answered :)" )
    }

    call.onhangup = () =>
    {
      console.log( "yay - hung up" );
    }
  }

  console.log( "Call originated" );

}, 1000 );

/* Our registration handlers */
projectcontrol.onreg = ( reg ) =>
{
  console.log( "onreg" );
  console.log( reg );
}

projectcontrol.ondereg = ( reg ) =>
{
  console.log( "ondereg" );
  console.log( reg );
}

/* Register our user */
projectcontrol.directory( "bling.babblevoice.com",
  [
    { "username": "1003", "secret": "1123654789" },
    { "username": "1000", "secret": "1123654789" }
  ] );

/* Wait for requests */
projectcontrol.run();
