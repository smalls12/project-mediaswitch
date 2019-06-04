

#include <boost/lexical_cast.hpp>

#include <stdexcept> // out of range

#include "projectsipdialog.h"
#include "projectsipconfig.h"
#include "projectsipregistrar.h"
#include "projectsipdirectory.h"
#include "projectsipsdp.h"
#include "globals.h"


static projectsipdialogs dialogs;
static unsigned int projectsipdialogcounter = 0;

/*!md
# projectsipdialog
Constructor
Updated: 23.01.2019
*/
projectsipdialog::projectsipdialog() :
  nextstate( std::bind( &projectsipdialog::invitestart, this, std::placeholders::_1 ) ),
  controlrequest( projecthttpclient::create( io_service ) ),
  timer( io_service ),
  retries( 3 ),
  authenticated( false ),
  originator( false ),
  remotesdp( JSON::Object() ),
  oursdp( JSON::Object() ),
  callringing( false ),
  callanswered( false ),
  callhungup( false ),
  startat( std::time(nullptr) ),
  ringingat( 0 ),
  answerat( 0 ),
  endat( 0 ),
  ourtag( projectsippacket::tag() )
{
  projectsipdialogcounter++;
}

/*!md
# projectsipdialog
Destructor
Updated: 23.01.2019
*/
projectsipdialog::~projectsipdialog()
{
  this->controlrequest->asynccancel();
}

/*!md
# create
*/
projectsipdialog::pointer projectsipdialog::create()
{
  return pointer( new projectsipdialog() );
}

/*!md
# invitesippacket
Handle a SIP invite packet. Return false if the packet was not intended for us (i.e. not part of a dialog) or true if it was.
*/
bool projectsipdialog::invitesippacket( projectsippacket::pointer pk )
{
  std::string callid = pk->getheader( projectsippacket::Call_ID ).str();

  if( 5 > callid.size() )
  {
    // Ignore small call ids.
    return true;
  }

  projectsipdialogs::index< projectsipdialogcallid >::type::iterator it;
  it = dialogs.get< projectsipdialogcallid >().find( callid );

  projectsipdialog::pointer ptr;

  if( dialogs.get< projectsipdialogcallid >().end() == it )
  {
    switch( pk->getmethod() )
    {
      case projectsippacket::RESPONSE:
      {
        return false;
        break;
      }
      case projectsippacket::INVITE:
      {
        // New dialog
        ptr = projectsipdialog::create();
        ptr->callid = callid;
        dialogs.insert( ptr );
        break;
      }
      default:
      {
        projectsippacket::pointer response = projectsippacket::create();

        response->setstatusline( 481, "Call Leg/Transaction Does Not Exist" );
        response->addviaheader( projectsipconfig::gethostipsipport(), pk.get() );

        response->addwwwauthenticateheader( pk.get() );

        response->addheader( projectsippacket::To,
                            pk->getheader( projectsippacket::To ) );
        response->addheader( projectsippacket::From,
                            pk->getheader( projectsippacket::From ) );
        response->addheader( projectsippacket::Call_ID,
                            pk->getheader( projectsippacket::Call_ID ) );
        response->addheader( projectsippacket::CSeq,
                            pk->getheader( projectsippacket::CSeq ) );
        response->addheader( projectsippacket::Contact,
                            projectsippacket::contact( pk->getuser().strptr(),
                            stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );
        response->addheader( projectsippacket::Allow,
                            "INVITE, ACK, CANCEL, OPTIONS, BYE" );
        response->addheader( projectsippacket::Content_Type,
                            "application/sdp" );
        response->addheader( projectsippacket::Content_Length,
                            "0" );

        pk->respond( response->strptr() );
        return false;
      }
    }
  }
  else
  {
    ptr = *it;
  }

  ptr->lastreceivedpk = pk;
  switch( pk->getmethod() )
  {
    /* There may be a reason I should seperate this out - but for now */
    case projectsippacket::BYE:
    case projectsippacket::CANCEL:
    {
      /* we always have to handle a BYE. */
      ptr->handlebye( pk );
      break;
    }
    default:
    {
      ptr->nextstate( pk );
      break;
    }
  }

  return true;
}

/*!md
Timer functions.
*/

/*!md
# ontimeoutenddialog
Timer handler to end this dialog on timeout.
*/
void projectsipdialog::ontimeoutenddialog( const boost::system::error_code& error )
{
  if ( error != boost::asio::error::operation_aborted )
  {
    this->untrack();
  }
}

/*!md
# ontimeout486andenddialog
Timer handler to end this dialog on timeout.
*/
void projectsipdialog::ontimeout486andenddialog( const boost::system::error_code& error )
{
  if ( error != boost::asio::error::operation_aborted )
  {
    this->temporaryunavailable();
  }
}

/*!md
# waitfortimer
Async wait for timer.
*/
void projectsipdialog::waitfortimer( std::chrono::milliseconds s,
      std::function<void ( const boost::system::error_code& error ) > f )
{
  this->timer.cancel();
  this->timer.expires_after( s );
  this->timer.async_wait( f );
}

/*!md
# canceltimer
Set the timer back to the default.
*/
void projectsipdialog::canceltimer( void )
{
  this->timer.cancel();
}

/*!md
# handlebye
We have received a BYE - clean up.
*/
void projectsipdialog::handlebye( projectsippacket::pointer pk )
{
  this->callhungup = true;
  this->endat = std::time( nullptr );
  this->lastreceivedpk = pk;
  this->send200();
  this->updatecontrol();
}

/*!md
State functions.
*/

/*!md
# invitestart
The start of a dialog state machine. We check we have a valid INVITE
and if we need to authenticate the user.
*/
void projectsipdialog::invitestart( projectsippacket::pointer pk )
{
  this->lastreceivedpk = pk;

  /* Do we need authenticating? Currently force authentication. But we will need also friendly IP to allow inbound calls. */
#warning finish off auth
  if ( 1 /* do we need authenticating */ )
  {
    this->retries = 3;
    this->send401();

    this->nextstate = std::bind( &projectsipdialog::waitforacktheninviteauth, this, std::placeholders::_1 );

    this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
        std::bind( &projectsipdialog::resend401, this, std::placeholders::_1 ) );
    return;
  }

  this->domain = pk->geturihost().str();

  if( pk->getheader( projectsippacket::Content_Length ).toint() > 0 &&
      0 != pk->getheader( projectsippacket::Content_Type ).find( "sdp" ).end() )
  {
    sdptojson( pk->getbody(), this->remotesdp );
  }

  this->invitepk = pk;
  this->updatecontrol();
  this->nextstate = std::bind( &projectsipdialog::waitfornextinstruction, this, std::placeholders::_1 );
  this->trying();
}

/*!md
# waitforacktheninviteauth
We have (probably) sent a auth request via an invite responce. We now have to get an Ack then the client will proceed to invite with auth.
*/
void projectsipdialog::waitforacktheninviteauth( projectsippacket::pointer pk )
{
  this->lastackpacket = pk;
  if( projectsippacket::ACK == pk->getmethod() )
  {
    this->ackpk = pk;
    this->canceltimer();
    this->nextstate = std::bind( &projectsipdialog::inviteauth, this, std::placeholders::_1 );
  }
}


/*!md
# send401
Send a 201 - request auth
*/
void projectsipdialog::send401( void )
{
  this->authrequest = projectsippacket::create();

  this->authrequest->setstatusline( 401, "Unauthorized" );
  this->authrequest->addviaheader( projectsipconfig::gethostipsipport(), this->lastreceivedpk.get() );

  this->authrequest->addwwwauthenticateheader( this->lastreceivedpk.get() );

  this->authrequest->addheader( projectsippacket::To,
                      this->lastreceivedpk->getheader( projectsippacket::To ) );
  this->authrequest->addheader( projectsippacket::From,
                      this->lastreceivedpk->getheader( projectsippacket::From ) );
  this->authrequest->addheader( projectsippacket::Call_ID,
                      this->lastreceivedpk->getheader( projectsippacket::Call_ID ) );
  this->authrequest->addheader( projectsippacket::CSeq,
                      this->lastreceivedpk->getheader( projectsippacket::CSeq ) );
  this->authrequest->addheader( projectsippacket::Contact,
                      projectsippacket::contact( this->lastreceivedpk->getuser().strptr(),
                      stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );

  this->authrequest->addheader( projectsippacket::Allow,
                      "INVITE, ACK, CANCEL, OPTIONS, BYE" );
  this->authrequest->addheader( projectsippacket::Content_Length, "0" );

  this->sendpk( this->authrequest );
}

/*!md
# resend401
Send a 401
*/
void projectsipdialog::resend401( const boost::system::error_code& error )
{
  if ( error != boost::asio::error::operation_aborted )
  {
    if( 0 == this->retries )
    {
      this->untrack();
      return;
    }

    this->send401();
    this->retries--;

    this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
        std::bind( &projectsipdialog::resend401, this, std::placeholders::_1 ) );
  }
}

/*!md
# inviteauth
We have requested authentication so this packet should contain auth information.
*/
void projectsipdialog::inviteauth( projectsippacket::pointer pk )
{

  if( projectsippacket::INVITE != pk->getmethod() )
  {
    return;
  }

  substring user = pk->getheaderparam( projectsippacket::Authorization, "username" );
  substring realm = pk->getheaderparam( projectsippacket::Authorization, "realm" );

  stringptr password = projectsipdirdomain::lookuppassword(
                realm,
                user );

  if( !password ||
        !pk->checkauth( this->authrequest.get(), password ) )
  {
    projectsippacket::pointer failedauth = projectsippacket::create();

    failedauth->setstatusline( 403, "Failed auth" );
    failedauth->addviaheader( projectsipconfig::gethostipsipport(), pk.get() );

    failedauth->addheader( projectsippacket::To,
                pk->getheader( projectsippacket::To ) );
    failedauth->addheader( projectsippacket::From,
                pk->getheader( projectsippacket::From ) );
    failedauth->addheader( projectsippacket::Call_ID,
                pk->getheader( projectsippacket::Call_ID ) );
    failedauth->addheader( projectsippacket::CSeq,
                pk->getheader( projectsippacket::CSeq ) );
    failedauth->addheader( projectsippacket::Contact,
                projectsippacket::contact( pk->getuser().strptr(),
                stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );
    failedauth->addheader( projectsippacket::Allow,
                "INVITE, ACK, CANCEL, OPTIONS, BYE" );
    failedauth->addheader( projectsippacket::Content_Length,
                        "0" );

    this->sendpk( failedauth );

    this->nextstate = std::bind( &projectsipdialog::waitforackanddie, this, std::placeholders::_1 );

    this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
        std::bind( &projectsipdialog::ontimeoutenddialog, this, std::placeholders::_1 ) );

    return;
  }

  this->domain = pk->geturihost().str();

  if( pk->getheader( projectsippacket::Content_Length ).toint() > 0 &&
      0 != pk->getheader( projectsippacket::Content_Type ).find( "sdp" ).end() )
  {
    sdptojson( pk->getbody(), this->remotesdp );
  }

  this->invitepk = pk;
  this->lastauthenticatedpk = pk;
  this->authenticated = true;
  this->updatecontrol();
  this->trying();
}

/*!md
# trying
Send 100 back to client.
*/
void projectsipdialog::trying( void )
{
  projectsippacket::pointer trying = projectsippacket::create();
  trying->setstatusline( 100, "Trying" );

  trying->addheader( projectsippacket::Via,
              this->lastreceivedpk->getheader( projectsippacket::Via ) );
  trying->addheader( projectsippacket::To,
              this->lastreceivedpk->getheader( projectsippacket::To ) );
  trying->addheader( projectsippacket::From,
              this->lastreceivedpk->getheader( projectsippacket::From ) );
  trying->addheader( projectsippacket::Call_ID,
              this->lastreceivedpk->getheader( projectsippacket::Call_ID ) );
  trying->addheader( projectsippacket::CSeq,
              this->lastreceivedpk->getheader( projectsippacket::CSeq ) );
  trying->addheader( projectsippacket::Contact,
              projectsippacket::contact( this->lastreceivedpk->getuser().strptr(),
              stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );
  trying->addheader( projectsippacket::Allow,
              "INVITE, ACK, CANCEL, OPTIONS, BYE" );
  trying->addheader( projectsippacket::Content_Length,
                      "0" );

  this->sendpk( trying );

  this->waitfortimer( std::chrono::milliseconds( DIALOGSETUPTIMEOUT ),
      std::bind( &projectsipdialog::ontimeout486andenddialog, this, std::placeholders::_1 ) );
}

/*!md
# temporaryunavailble
Send 480 back to client.
*/
void projectsipdialog::temporaryunavailable( void )
{
  projectsippacket::pointer unavail = projectsippacket::create();

  unavail->setstatusline( 480, "Temporarily Unavailable" );
  unavail->addviaheader( projectsipconfig::gethostipsipport(), this->lastreceivedpk.get() );

  unavail->addheader( projectsippacket::To,
              this->lastreceivedpk->getheader( projectsippacket::To ) );
  unavail->addheader( projectsippacket::From,
              this->lastreceivedpk->getheader( projectsippacket::From ) );
  unavail->addheader( projectsippacket::Call_ID,
              this->lastreceivedpk->getheader( projectsippacket::Call_ID ) );
  unavail->addheader( projectsippacket::CSeq,
              this->lastreceivedpk->getheader( projectsippacket::CSeq ) );
  unavail->addheader( projectsippacket::Contact,
              projectsippacket::contact( this->lastreceivedpk->getuser().strptr(),
              stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );
  unavail->addheader( projectsippacket::Allow,
              "INVITE, ACK, CANCEL, OPTIONS, BYE" );
  unavail->addheader( projectsippacket::Content_Length,
                      "0" );

  this->sendpk( unavail );

  // We should receive a ACK - at that point the dialog can be closed.
  this->nextstate = std::bind( &projectsipdialog::waitforackanddie, this, std::placeholders::_1 );

  this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
      std::bind( &projectsipdialog::ontimeoutenddialog, this, std::placeholders::_1 ) );
}

/*!md
# waitforack
*/
void projectsipdialog::waitforack( projectsippacket::pointer pk )
{
  this->lastackpacket = pk;
  if( projectsippacket::ACK == pk->getmethod() )
  {
    this->ackpk = pk;
    this->canceltimer();
    this->updatecontrol();
  }
}

/*!md
# waitforackanddie
The next ack we recieve will end this dialog.
*/
void projectsipdialog::waitforackanddie( projectsippacket::pointer pk )
{
  // When we get an ACK to the last message we can die.
  if( projectsippacket::ACK == pk->getmethod() )
  {
    this->ackpk = pk;
    this->updatecontrol();
    this->untrack();
  }
}

void projectsipdialog::waitfor200anddie( projectsippacket::pointer pk )
{
  int code = pk->getstatuscode();
  if( 200 == code || 481 == code )
  {
    this->updatecontrol();
    this->untrack();
  }
}

/*!md
# waitfornextinstruction
We have received an INVITE and sent a trying, if the client has
not recevieved the trying then we will get another INVITE. So send trying again
whilst we wait for our next control instruction.
*/
void projectsipdialog::waitfornextinstruction( projectsippacket::pointer pk )
{
  this->lastreceivedpk = pk;

  switch( pk->getmethod() )
  {
    case projectsippacket::INVITE:
    {
      this->trying();
      break;
    }
  }

  /* Wait up to DIALOGSETUPTIMEOUT seconds before we close this dialog */
  this->waitfortimer( std::chrono::milliseconds( DIALOGSETUPTIMEOUT ),
      std::bind( &projectsipdialog::ontimeoutenddialog, this, std::placeholders::_1 ) );
}

/*!md
# ringing
Send a 180 ringing
*/
void projectsipdialog::ringing( void )
{
  if( !this->callringing && !this->callanswered && !this->callhungup )
  {
    projectsippacket::pointer ringing = projectsippacket::create();

    ringing->setstatusline( 180, "Ringing" );

    ringing->addheader( projectsippacket::Via,
              this->invitepk->getheader( projectsippacket::Via ) );

    sipuri u( this->invitepk->getheader( projectsippacket::To ) );
    substring uri = u.uri;
    if( 0 != uri.end() )
    {
      /* include the <> */
      uri.start( uri.start() - 1 );
      uri.end( uri.end() + 1 );
    }

    ringing->addheader( projectsippacket::To,
                uri.str() + ";tag=" + *this->ourtag );
    ringing->addheader( projectsippacket::From,
                this->invitepk->getheader( projectsippacket::From ) );
    ringing->addheader( projectsippacket::Call_ID,
                this->invitepk->getheader( projectsippacket::Call_ID ) );
    ringing->addheader( projectsippacket::CSeq,
                this->invitepk->getheader( projectsippacket::CSeq ) );
    ringing->addheader( projectsippacket::Contact,
                projectsippacket::contact( this->invitepk->getuser().strptr(),
                stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );
    ringing->addheader( projectsippacket::Allow,
                "INVITE, ACK, CANCEL, OPTIONS, BYE" );
    ringing->addheader( projectsippacket::Content_Length,
                        "0" );

    if( 0 != this->alertinfo.size() )
    {
      ringing->addheader( projectsippacket::Alert_Info,
                this->alertinfo );
    }

    this->sendpk( ringing );

    this->canceltimer();
    this->callringing = true;
    this->ringingat = std::time( nullptr );

    this->waitfortimer( std::chrono::milliseconds( DIALOGSETUPTIMEOUT ),
      std::bind( &projectsipdialog::ontimeout486andenddialog, this, std::placeholders::_1 ) );
  }
}

#warning TODO
/*!md
# preanswer
Send a 183 with SDP - early media (media before answer i.e. fake ringing)
*/

/*!md
# answer
Send a 200 answer
*/
void projectsipdialog::answer( std::string body )
{
  if( !this->callanswered )
  {
    this->retries = 3;
    this->send200( body );
    this->nextstate = std::bind( &projectsipdialog::waitforack, this, std::placeholders::_1 );
    this->callanswered = true;
    this->answerat = std::time( nullptr );

      /* Wait up to 60 seconds before we close this dialog */
    this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
      std::bind( &projectsipdialog::ontimeoutenddialog, this, std::placeholders::_1 ) );
  }
}

/*!md
# send200
Send a 200
*/
void projectsipdialog::send200( std::string body, bool final )
{
  projectsippacket::pointer ok = projectsippacket::create();

  ok->setstatusline( 200, "Ok" );
  ok->addviaheader( projectsipconfig::gethostipsipport(), this->lastreceivedpk.get() );

  sipuri tu( this->lastreceivedpk->getheader( projectsippacket::To ) );
  substring stu = tu.uri;
  if( 0 != stu.end() )
  {
    stu.start( stu.start() - 1 );
    stu.end( stu.end() + 1 );
  }
  ok->addheader( projectsippacket::To,
                stu.str() + ";tag=" + *this->ourtag );

  ok->addheader( projectsippacket::From,
              this->lastreceivedpk->getheader( projectsippacket::From ) );
  ok->addheader( projectsippacket::Call_ID,
              this->lastreceivedpk->getheader( projectsippacket::Call_ID ) );
  ok->addheader( projectsippacket::CSeq,
              this->lastreceivedpk->getheader( projectsippacket::CSeq ) );

  ok->addheader( projectsippacket::Contact,
              projectsippacket::contact( this->lastreceivedpk->getuser().strptr(),
              stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );

  ok->addheader( projectsippacket::Allow,
              "INVITE, ACK, CANCEL, OPTIONS, BYE" );

  ok->addheader( projectsippacket::Content_Length,
                      std::to_string( body.length() ) );

  if( body.length() > 0 )
  {
    ok->addheader( projectsippacket::Content_Type, "application/sdp" );
    ok->setbody( body.c_str() );
  }

  this->sendpk( ok );

  if( final )
  {
    this->nextstate = std::bind( &projectsipdialog::waitforackanddie, this, std::placeholders::_1 );
  }
  else
  {
    this->nextstate = std::bind( &projectsipdialog::waitforack, this, std::placeholders::_1 );
  }

  this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
      std::bind( &projectsipdialog::resend200, this, std::placeholders::_1 ) );
}


/*!md
# resend200
Send a 200
Updated: 06.02.2019
*/
void projectsipdialog::resend200( const boost::system::error_code& error )
{
  if ( error != boost::asio::error::operation_aborted )
  {
    if( 0 == this->retries )
    {
      this->untrack();
      return;
    }

    this->send200();
    this->retries--;

    this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
        std::bind( &projectsipdialog::resend200, this, std::placeholders::_1 ) );
  }
}

/*!md
# hangup
Hangup if ansered (BYE) or send reason for error code.
*/
void projectsipdialog::hangup( void )
{
  /* tehcnically we should only consider us hung up when we receive an ack or we have timed out waiting */
  this->callhungup = true;
  this->endat = std::time( nullptr );

  if( this->callanswered )
  {
    this->sendbye();
  }
  else
  {
    this->retries = 3;

    this->senderror();
    this->nextstate = std::bind( &projectsipdialog::waitforackanddie, this, std::placeholders::_1 );

      /* Wait up to DIALOGACKTIMEOUT seconds before we close this dialog */
    this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
      std::bind( &projectsipdialog::ontimeoutenddialog, this, std::placeholders::_1 ) );
  }
}

/*!md
# senderror
Send an error
*/
void projectsipdialog::senderror( void )
{
  projectsippacket::pointer ok = projectsippacket::create();

  ok->setstatusline( this->errorcode, this->errorreason );
  ok->addviaheader( projectsipconfig::gethostipsipport(), this->lastreceivedpk.get() );

  ok->addheader( projectsippacket::To,
              this->lastreceivedpk->getheader( projectsippacket::To ) );
  ok->addheader( projectsippacket::From,
              this->lastreceivedpk->getheader( projectsippacket::From ) );
  ok->addheader( projectsippacket::Call_ID,
              this->lastreceivedpk->getheader( projectsippacket::Call_ID ) );
  ok->addheader( projectsippacket::CSeq,
              this->lastreceivedpk->getheader( projectsippacket::CSeq ) );
  ok->addheader( projectsippacket::Contact,
              projectsippacket::contact( this->lastreceivedpk->getuser().strptr(),
              stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );
  ok->addheader( projectsippacket::Allow,
              "INVITE, ACK, CANCEL, OPTIONS, BYE" );
  ok->addheader( projectsippacket::Content_Length,
                      "0" );

  this->sendpk( ok );

  this->nextstate = std::bind( &projectsipdialog::waitforackanddie, this, std::placeholders::_1 );

  this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
      std::bind( &projectsipdialog::resenderror, this, std::placeholders::_1 ) );
}

/*!md
# resenderror
Send a 200
*/
void projectsipdialog::resenderror( const boost::system::error_code& error )
{
  if ( error != boost::asio::error::operation_aborted )
  {
    if( 0 == this->retries )
    {
      this->untrack();
      return;
    }

    this->senderror();
    this->retries--;

    this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
        std::bind( &projectsipdialog::resenderror, this, std::placeholders::_1 ) );
  }
}

/*!md
# sendbye
Send a BYE. Direction matters!
*/
void projectsipdialog::sendbye( void )
{
  if( !this->lastreceivedpk )
  {
    return;
  }

  if( this->originator )
  {
    if( !this->invitepk )
    {
      return;
    }
  }
  else
  {
    if( !this->ackpk )
    {
      return;
    }
  }

  projectsippacket::pointer bye = projectsippacket::create();

  if( this->originator )
  {
    bye->setrequestline( projectsippacket::BYE, this->invitepk->getrequesturi().str() );
    bye->addheader( projectsippacket::To,
                  this->lastreceivedpk->getheader( projectsippacket::To ) );

    bye->addheader( projectsippacket::From,
                this->lastreceivedpk->getheader( projectsippacket::From ) );
  }
  else
  {
    bye->setrequestline( projectsippacket::BYE, this->ackpk->getrequesturi().str() );
    bye->addheader( projectsippacket::To,
                  this->lastreceivedpk->getheader( projectsippacket::From ) );

    bye->addheader( projectsippacket::From,
                this->lastreceivedpk->getheader( projectsippacket::To ) );
  }

  bye->addviaheader( projectsipconfig::gethostipsipport() );

  bye->addheader( projectsippacket::Contact,
              projectsippacket::contact( this->lastreceivedpk->getuser().strptr(),
              stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );

  bye->addheader( projectsippacket::Call_ID, this->callid );

  bye->addheader( projectsippacket::CSeq,
              std::to_string( this->lastreceivedpk->getcseq() + 1 ) + " BYE" );

  bye->addheader( projectsippacket::Allow,
              "INVITE, ACK, CANCEL, OPTIONS, BYE" );

  bye->addheader( projectsippacket::Content_Length, "0" );

  this->sendpk( bye );

  this->nextstate = std::bind( &projectsipdialog::waitfor200anddie, this, std::placeholders::_1 );

  this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
      std::bind( &projectsipdialog::resendbye, this, std::placeholders::_1 ) );

}

/*!md
# resendbye
Resend the last bye
*/
void projectsipdialog::resendbye( const boost::system::error_code& error )
{
  if ( error != boost::asio::error::operation_aborted )
  {
    if( 0 == this->retries )
    {
      this->updatecontrol();
      this->untrack();
      return;
    }

    this->sendbye();
    this->retries--;

    this->waitfortimer( std::chrono::milliseconds( DIALOGACKTIMEOUT ),
        std::bind( &projectsipdialog::resendbye, this, std::placeholders::_1 ) );
  }
}

/*!md
# sendinvite
Send an INVITE. We will need to make a couple of decisions on how
and where to send the INVITE. For a registered INVITE this will be simpler.
Updated: 08.02.2019

We need to be able to send an INVITE to a registered user and also an external provider. This means we need to support SRV or equivalent methods. For example, 01442299280@gw.magrathea.net:5080 or 1000@bling.babblevoice.com

We need to add a method: post as it is not idempotent (i.e. multple calls will create mutliple invites).
POST will be new (i.e. create a call-id) PUT will create a re-invite (used for hold etc).

invite object:=
{
  to = {
    user: "01442299280",
    domain: "gw.magrathea.net",
    port: 5080,
    authuser: "authuser"
    authsecret: "secret"
  }

  from = {
    user: "1000",
    domain: "bling.babblevoice.com" // If not present it will take the domain from to.
  }

  autoanswer = true ( for intercom=true and answer-after=0 )

  sdp = {...}

  maxforwards = 70 (or source dialog maxforwards - 1 )

  cid = {
    name: "",
    number: "",
    private: false
  }
}

We return
{ callid: "somenewcallid" }

contact would become
<sip:01442299280@127.0.0.1:9997>

We need to populate something like (see below my comments)

URI - in this example: sip:1002@82.19.197.210:5070;transport=udp;received=82.19.197.210:5070;intercom=true
Max forwards
From: "1002" <sip:3@bling.babblevoice.com> (we add the tag)
To: <sip:1002@82.19.197.210:5070;transport=udp;received=82.19.197.210:5070>

We will generate the callid and cseq, contact?

Contact should be <sip:user@ip:port>

Call-info is added for auto answer (which is what babble's click dial works on)


INVITE sip:1002@82.19.197.210:5070;transport=udp;received=82.19.197.210:5070;intercom=true SIP/2.0
Via: SIP/2.0/UDP 109.169.12.139:9997;rport;branch=z9hG4bKZpt81XHSNaDtK
Max-Forwards: 70
From: "1002" <sip:3@bling.babblevoice.com>;tag=tXUKmjUNv4BaD
To: <sip:1002@82.19.197.210:5070;transport=udp;received=82.19.197.210:5070>
Call-ID: 6ace2ce2-f281-1237-bf93-001b78073992
CSeq: 4452880 INVITE
Contact: <sip:1002@109.169.12.139:9997>
Call-Info: <sip:109.169.12.139>;answer-after=0
User-Agent: FreeSWITCH
Allow: INVITE, ACK, BYE, CANCEL, OPTIONS, MESSAGE, INFO, UPDATE, REGISTER, REFER, NOTIFY, PUBLISH, SUBSCRIBE
Supported: timer, path, replaces
Allow-Events: talk, hold, conference, presence, as-feature-event, dialog, line-seize, call-info, sla, include-session-description, presence.winfo, message-summary, refer
Session-Expires: 7200;refresher=uac
Min-SE: 1800
Content-Type: application/sdp
Content-Disposition: session
Content-Length: 320
X-FS-Support: update_display,send_info
Remote-Party-ID: "1002" <sip:3@bling.babblevoice.com>;party=calling;screen=yes;privacy=off

v=0
o=FreeSWITCH 1557996833 1557996834 IN IP4 109.169.12.139
s=FreeSWITCH
c=IN IP4 109.169.12.139
t=0 0
m=audio 16512 RTP/AVP 9 8 101 13
a=rtpmap:9 G722/8000
a=rtpmap:8 PCMA/8000
a=rtpmap:101 telephone-event/8000
a=fmtp:101 0-16
a=rtpmap:13 CN/8000
a=rtcp-mux
a=rtcp:16512 IN IP4 109.169.12.139
a=ptime:20

*/
void projectsipdialog::sendinvite( JSON::Object &request, projectwebdocument &response )
{
  std::string touser, todomain, fromuser, callerid, calleridname, sdp;
  int maxforwards;
  bool hide = false;

  try
  {
    JSON::Object &from = JSON::as_object( request[ "from" ] );
    this->domain = JSON::as_string( from[ "domain" ] );
    fromuser = JSON::as_string( from[ "user" ] );

    JSON::Object &to = JSON::as_object( request[ "to" ] );
    if( to.has_key( "domain" ) )
    {
      todomain = JSON::as_string( to[ "domain" ] );
    }
    else
    {
      todomain = this->domain;
    }

    touser = JSON::as_string( to[ "user" ] );

    maxforwards = JSON::as_int64( request[ "maxforwards" ] );

    JSON::Object &cid =  JSON::as_object( request[ "cid" ] );
    callerid = JSON::as_string( cid[ "number" ] );
    calleridname = JSON::as_string( cid[ "name" ] );

    if ( cid.has_key( "private" ) )
    {
      hide = ( bool )JSON::as_boolean( cid[ "private" ] );
    }

    this->oursdp = request[ "sdp" ];
    sdp = *( jsontosdp( JSON::as_object( request[ "sdp" ] ) ) );
  }
  catch( const boost::bad_get &e )
  {
    // We have not got enough params.
    this->untrack();
    response.setstatusline( 500, "Missing param or params in invite" );
    response.addheader( projectwebdocument::Content_Length, 0 );
    return;
  }

  projectsippacket::pointer invite = projectsippacket::create();
  // Generate SIP URI i.e. sip:realm
  invite->setrequestline( projectsippacket::INVITE, std::string( "sip:" ) + todomain );
  invite->addviaheader( projectsipconfig::gethostipsipport() );

  invite->addheader( projectsippacket::Contact,
              projectsippacket::contact( stringptr( new std::string( fromuser ) ),
              stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );

  invite->addheader( projectsippacket::Call_ID, this->callid );
#warning TODO
  /* TODO - need to correct the nuber - i.e. re-invite may be different. */
  invite->addheader( projectsippacket::CSeq, "1 INVITE" );
  invite->addheader( projectsippacket::Max_Forwards, maxforwards );
  invite->addheader( projectsippacket::Allow,
              "INVITE, ACK, CANCEL, OPTIONS, BYE" );

  invite->addheader( projectsippacket::Content_Type,
                      "application/sdp" );
  invite->addheader( projectsippacket::Content_Length,
                      sdp.length() );

  invite->addremotepartyid( this->domain.c_str(), calleridname.c_str(), callerid.c_str(), hide );

  std::string touri;
  touri.reserve( 200 );
  touri = "<sip:";
  touri += touser;
  touri += "@";
  touri += todomain;
  touri += ">";

  invite->addheader( projectsippacket::To, touri ); /* only gets a tag on a re-invite */

  std::string fromuri;
  fromuri.reserve( 200 );
  fromuri = "<sip:";
  fromuri += fromuser;
  fromuri += "@";
  fromuri += this->domain;
  fromuri += ">;tag=";
  fromuri += *this->ourtag;

  invite->addheader( projectsippacket::From, fromuri ); /* always needs a tag */

  /* after all headers have been added */
  invite->setbody( sdp.c_str() );

  if( !this->sendpk( invite ) )
  {
    this->untrack();
    response.setstatusline( this->errorcode, this->errorreason );
    response.addheader( projectwebdocument::Content_Length, 0 );
    return;
  }

  // Respond to the web request.
  JSON::Object v;
  v[ "callid" ] = this->callid;
  std::string t = JSON::to_string( v );

  response.setstatusline( 200, "Ok" );
  response.addheader( projectwebdocument::Content_Length, t.size() );
  response.addheader( projectwebdocument::Content_Type, "text/json" );
  response.setbody( t.c_str() );

  this->invitepk = invite;

  this->nextstate = std::bind( &projectsipdialog::waitforinviteprogress, this, std::placeholders::_1 );

}

/*!md
# sendack
We have received a 200 send an Ack.
*/
void projectsipdialog::sendack( void )
{
  projectsippacket::pointer ack = projectsippacket::create();
  // Generate SIP URI i.e. sip:realm
  ack->setrequestline( projectsippacket::ACK, std::string( "sip:" ) + this->domain );
  ack->addviaheader( projectsipconfig::gethostipsipport() );

  ack->addheader( projectsippacket::To,
                this->lastreceivedpk->getheader( projectsippacket::To ) );

  ack->addheader( projectsippacket::From,
              this->lastreceivedpk->getheader( projectsippacket::From ) );

  ack->addheader( projectsippacket::Contact,
              projectsippacket::contact( this->lastreceivedpk->getuser().strptr(),
              stringptr( new std::string( projectsipconfig::gethostipsipport() ) ) ) );

  ack->addheader( projectsippacket::Call_ID, this->callid );
#warning TODO
  /* TODO - need to correct the number - i.e. re-invite may be different. */
  ack->addheader( projectsippacket::CSeq, "1 ACK" );
  ack->addheader( projectsippacket::Allow,
              "INVITE, ACK, CANCEL, OPTIONS, BYE" );

  ack->addheader( projectsippacket::Content_Length, "0" );

  this->sendpk( ack );
}

/*!md
# waitforinviteprogress
We have sent an invite - wait for something to come back.
*/
void projectsipdialog::waitforinviteprogress( projectsippacket::pointer pk )
{
  int statuscode = pk->getstatuscode();
  if( 180 == statuscode )
  {
    this->callringing = true;
    this->ringingat = std::time( nullptr );
    this->updatecontrol();
  }
  else if( 200 == statuscode )
  {
    this->callanswered = true;
    this->answerat = std::time( nullptr );

    if( pk->getheader( projectsippacket::Content_Length ).toint() > 0 &&
        0 != pk->getheader( projectsippacket::Content_Type ).find( "sdp" ).end() )
    {
      sdptojson( pk->getbody(), this->remotesdp );
    }

    this->sendack();
    this->updatecontrol();
  }
  else if( statuscode >= 400 && statuscode < 500 )
  {
    this->errorcode = statuscode;
    this->errorreason = pk->getreasonphrase().str();
    this->callhungup = true;
    this->endat = std::time( nullptr );
    this->updatecontrol();
  }
}

/*!md
# untrack
Remove from our container so we can clean up.
*/
void projectsipdialog::untrack( void )
{
  this->timer.cancel();

  projectsipdialogs::index< projectsipdialogcallid >::type::iterator it;
  it = dialogs.get< projectsipdialogcallid >().find( this->callid );
  if( dialogs.get< projectsipdialogcallid >().end() != it )
  {
    dialogs.erase( it );
  }
}

/*!md
Control/HTTP functions.
*/

/*!md
# updatecontrol
Pass information to our control server.
*/
bool projectsipdialog::updatecontrol( void )
{
  projectsipdirdomain::pointer ptr = projectsipdirdomain::lookupdomain( this->domain );

  if( !ptr )
  {
    return false;
  }

  std::string controluri = *ptr->controlhost;
  controluri += "/dialog/" + this->callid;

  projectwebdocumentptr d = projectwebdocumentptr( new projectwebdocument() );
  d->setrequestline( projectwebdocument::PUT, controluri );

  JSON::Object v;

  v[ "callid" ] = this->callid;
  v[ "domain" ] = this->domain;

  if( this->invitepk )
  {
    v[ "to" ] = this->invitepk->getuser( projectsippacket::To ).str();
    v[ "from" ] = this->invitepk->getuser( projectsippacket::From ).str();
    v[ "contact" ] = this->invitepk->getheader( projectsippacket::Contact ).str();
    v[ "maxforwards" ] = ( JSON::Integer ) this->invitepk->getheader( projectsippacket::Max_Forwards ).toint();
  }
  v[ "authenticated" ] = ( JSON::Bool ) this->authenticated;
  v[ "ring" ] = ( JSON::Bool ) this->callringing;
  v[ "answered" ] = ( JSON::Bool ) this->callanswered;
  v[ "originator" ] = ( JSON::Bool ) this->originator;
  v[ "hangup" ] = ( JSON::Bool ) this->callhungup;

  JSON::Object er;
  er[ "code" ] = ( JSON::Integer ) this->errorcode;
  er[ "status" ] = this->errorreason;
  v[ "error" ] = er;

  if( this->lastreceivedpk )
  {
    JSON::Object rh;
    rh[ "host" ] = this->lastreceivedpk->getremotehost();
    rh[ "port" ] = boost::numeric_cast< JSON::Integer >( this->lastreceivedpk->getremoteport() );
    v[ "remote" ] = rh;
  }

  JSON::Object sdp;
  sdp[ "them" ] = this->remotesdp;
  sdp[ "us" ] = this->oursdp;
  v[ "sdp" ] = sdp;

  JSON::Object epochs;
  epochs[ "start" ] = boost::numeric_cast< JSON::Integer >( this->startat );
  epochs[ "ring" ] = boost::numeric_cast< JSON::Integer >( this->ringingat );
  epochs[ "answer" ] = boost::numeric_cast< JSON::Integer >( this->answerat );
  epochs[ "end" ] = boost::numeric_cast< JSON::Integer >( this->endat );

  v[ "epochs" ] = epochs;

  std::string t = JSON::to_string( v );
  d->addheader( projectwebdocument::Content_Length, t.length() );
  d->addheader( projectwebdocument::Content_Type, "text/json" );
  d->setbody( t.c_str() );

  this->controlrequest->asyncrequest( d, std::bind( &projectsipdialog::httpcallback, this, std::placeholders::_1 ) );

  return true;
}

/*!md
# httpcallback
We have made a request to our control server. This is a do nothing if
no error occurred.
Updated: 23.01.2019
*/
void projectsipdialog::httpcallback( int errorcode )
{
  if( this->callhungup )
  {
    this->untrack();
    return;
  }

  if( 0 != errorcode )
  {
    this->temporaryunavailable();
    return;
  }

  projectwebdocumentptr r = this->controlrequest->getresponse();

  if( !r || 200 != r->getstatuscode() )
  {
    this->temporaryunavailable();
    return;
  }
  /* We are now idle until we receive our next instruction. */
}

/*
httpget
Handle a request from our web server.
*/
void projectsipdialog::httpget( stringvector &path, projectwebdocument &response )
{
  JSON::Object v;
  v[ "count" ] = ( JSON::Integer ) dialogs.size();
  v[ "counter" ] = ( JSON::Integer ) projectsipdialogcounter;
  std::string t = JSON::to_string( v );

  response.setstatusline( 200, "Ok" );
  response.addheader( projectwebdocument::Content_Length, t.length() );
  response.addheader( projectwebdocument::Content_Type, "text/json" );
  response.setbody( t.c_str() );
}

/*!md
# httpost
Handle our post requests. POST = CREATE (ish).

See the notes on the invite function for the structure of the invite object.
*/
void projectsipdialog::httppost( stringvector &path, JSON::Value &body, projectwebdocument &response )
{
  std::string action = path[ 1 ];

  if( "invite" == action )
  {
    projectsipdialog::pointer ptr;
    ptr = projectsipdialog::create();
    ptr->callid = *projectsippacket::callid();
    ptr->originator = true;
    dialogs.insert( ptr );
    ptr->sendinvite( JSON::as_object( body ), response );
    return;
  }
}

/*!md
# httpput
Handle a request from our web server.
*/
void projectsipdialog::httpput( stringvector &path, JSON::Value &body, projectwebdocument &response )
{
  JSON::Object b = JSON::as_object( body );

  if( path.size() < 1 )
  {
    response.setstatusline( 404, "Not found" );
    response.addheader( projectwebdocument::Content_Length, 0 );
    return;
  }

  if( !b.has_key( "callid" ) )
  {
    response.setstatusline( 404, "Not found" );
    response.addheader( projectwebdocument::Content_Length, 0 );
    return;
  }

  std::string action = path[ 1 ];

  projectsipdialogs::index< projectsipdialogcallid >::type::iterator it;
  it = dialogs.get< projectsipdialogcallid >().find( JSON::as_string( b[ "callid" ] ) );
  if( dialogs.get< projectsipdialogcallid >().end() == it )
  {
    response.setstatusline( 404, "Not found" );
    response.addheader( projectwebdocument::Content_Length, 0 );
    return;
  }

  if( action == "invite" )
  {
    /* this will be a re-invite */
    return;
  }

  if( action == "ring" )
  {
    response.setstatusline( 200, "Ok" );
    (*it)->alertinfo = "";
    if ( b.has_key( "alertinfo" ) )
    {
      (*it)->alertinfo = JSON::as_string( b[ "alertinfo" ] );
    }
    (*it)->ringing();
  }
  else if( action == "answer" )
  {
    response.setstatusline( 200, "Ok" );

    // need to add sdp info.
    (*it)->retries = 3;

    std::string sdp = "";
    if ( b.has_key( "sdp" ) )
    {
      sdp = *( jsontosdp( JSON::as_object( b[ "sdp" ] ) ) );
    }

    (*it)->answer( sdp );
  }
  else if( action == "hangup" )
  {
    response.setstatusline( 200, "Ok" );

    (*it)->retries = 3;

    if ( b.has_key( "reason" ) )
    {
      (*it)->errorreason = JSON::as_string( b[ "reason" ] );
    }
    else
    {
      (*it)->errorreason = "";
    }

    if ( b.has_key( "code" ) )
    {
      (*it)->errorcode = JSON::as_int64( b[ "code" ] );
    }
    else
    {
      /* Default - not found */
      (*it)->errorcode = 404;
    }

    (*it)->hangup();
  }
  else
  {
    response.setstatusline( 404, "Not found" );
  }

  response.addheader( projectwebdocument::Content_Length, 0 );
}
