

#include <iostream>
#include <stdio.h>
#include <signal.h>

#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>

#include <sys/resource.h>

#include <fstream>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <string>

#include "projectsipserver.h"
#include "projectsippacket.h"

/*******************************************************************************
Function: readfile
Purpose: Read a file into a std::string
Updated: 13.12.2018
*******************************************************************************/
std::string readfile( std::string file )
{
  std::ifstream in;
  in.open( file );
  std::stringstream sstr;
  sstr << in.rdbuf();
  return sstr.str();
}

/*******************************************************************************
Function: gettestchunk
Purpose: Returns a block seperated by a start tag and end tag.

==TAG==
This is what is returned.
==END==

Updated: 14.12.2018
*******************************************************************************/
std::string gettestchunk( std::string &contents, std::string tag)
{
  size_t startpos = contents.find( "==" + tag + "==\r\n" );
  size_t endpos = contents.find( "\r\n==END==", startpos );

  if( std::string::npos == startpos || std::string::npos == endpos )
  {
    return "";
  }

  startpos += 6 + tag.size();

  if( startpos > contents.size() )
  {
    return "";
  }

  if( endpos > contents.size() )
  {
    return "";
  }

  return contents.substr( startpos, endpos - startpos );
}

/*******************************************************************************
Function: test
Purpose: Macro to call a funtion and test the result and display accordingly.
Updated: 13.12.2018
*******************************************************************************/
#define test(s, d, e) if( s != d ){ std::cout << __FILE__ << ":" << __LINE__ << " " << e << " '" << s << "' != '" << d << "'" << std::endl; return; }


/*******************************************************************************
Function: testsippacket
Purpose: sippacket tests
To ensure the test files have the correct formatting, use:
todos siptest1.txt (todos is found in the debian tofrodos package)
Updated: 13.12.2018
*******************************************************************************/
void testsippacket( void )
{
  std::string testdata = readfile( "../testfiles/siptest1.txt" );

  {
    projectsippacket testpacket( gettestchunk( testdata, "TEST1" ) );

    test( testpacket.getmethod(), projectsippacket::REGISTER , "Invalid method");

    test( testpacket.getheader( projectsippacket::Call_ID ),
            "843817637684230@998sdasdh09",
            "Wrong Call ID." );

    test( testpacket.getheader( projectsippacket::To ),
            "Bob <sip:bob@biloxi.com>",
            "Wrong To header field." );

    test( testpacket.getrequesturi(),
            "sip:registrar.biloxi.com",
            "Wrong Request URI." );

    test( testpacket.hasheader( projectsippacket::Authorization ),
            false,
            "We don't have a Authorization header." )

    test( testpacket.hasheader( projectsippacket::From ),
            true,
            "We should have a From header." )


  }

  {
    projectsippacket testpacket( gettestchunk( testdata, "TEST2" ) );

    test( testpacket.getstatuscode(), 180, "Expecting 180." );

    test( testpacket.getheader( projectsippacket::Contact ),
            "<sip:bob@192.0.2.4>",
            "Wrong Contact field." );

    test( testpacket.getheader( projectsippacket::Call_ID ),
            "a84b4c76e66710",
            "Wrong Call ID." );

    test( testpacket.getheader( projectsippacket::Content_Type ),
            "multipart/signed;\r\n "
            "protocol=\"application/pkcs7-signature\";\r\n "
            "micalg=sha1; boundary=boundary42",
            "Wrong Content Type." );

    test( testpacket.getheader( projectsippacket::Via ),
            "SIP/2.0/UDP server10.biloxi.com;branch=z9hG4bK4b43c2ff8.1\r\n "
            ";received=192.0.2.3",
            "Wrong Content Type." );

  }

  std::cout << "All sippacket tests passed, looking good" << std::endl;
}


/*******************************************************************************
Function: testurl
Purpose: URL class tests
Updated: 17.12.2018
*******************************************************************************/
void testurl( void )
{
  {
    std::string u( "http://myhost/my/big/path?myquerystring" );
    httpuri s( u );

    test( s.protocol.substr( u ), "http", "Bad protocol." );
    test( s.host.substr( u ), "myhost", "Bad host." );
    test( s.path.substr( u ), "/my/big/path", "Bad path." );
    test( s.query.substr( u ), "myquerystring", "Bad query." );
  }

  {
    std::string u( "\"Bob\" <sips:bob@biloxi.com> ;tag=a48s" );
    sipuri s( u );

    test( s.displayname.substr( u ), "Bob", "Bad Display name." );
    test( s.protocol.substr( u ), "sips", "Bad proto." );
    test( s.user.substr( u ), "bob", "Bad user." );
    test( s.host.substr( u ), "biloxi.com", "Bad host." );
    test( s.parameters.substr( u ), "tag=a48s", "Bad params." );
  }

  {
    std::string u( "Anonymous <sip:c8oqz84zk7z@privacy.org>;tag=hyh8" );
    sipuri s( u );

    test( s.displayname.substr( u ), "Anonymous", "Bad Display name." );
    test( s.protocol.substr( u ), "sip", "Bad proto." );
    test( s.user.substr( u ), "c8oqz84zk7z", "Bad user." );
    test( s.host.substr( u ), "privacy.org", "Bad host." );
    test( s.parameters.substr( u ), "tag=hyh8", "Bad params." );
  }

  {
    std::string u( "sip:+12125551212@phone2net.com;tag=887s" );
    sipuri s( u );

    test( s.displayname.substr( u ), "", "Bad Display name." );
    test( s.protocol.substr( u ), "sip", "Bad proto." );
    test( s.user.substr( u ), "+12125551212", "Bad user." );
    test( s.host.substr( u ), "phone2net.com", "Bad host." );
    test( s.parameters.substr( u ), "tag=887s", "Bad params." );
    test( s.getparameter( u, "tag" ).substr( u ), "887s", "Bad tag param." );
  }

  {
    std::string u( "<sip:+12125551212@phone2net.com>;tag=887s;blah=5566654gt" );
    sipuri s( u );

    test( s.displayname.substr( u ), "", "Bad Display name." );
    test( s.protocol.substr( u ), "sip", "Bad proto." );
    test( s.user.substr( u ), "+12125551212", "Bad user." );
    test( s.host.substr( u ), "phone2net.com", "Bad host." );
    test( s.parameters.substr( u ), "tag=887s;blah=5566654gt", "Bad params." );
    test( s.getparameter( u, "tag" ).substr( u ), "887s", "Bad tag param." );
    test( s.getparameter( u, "blah" ).substr( u ), "5566654gt", "Bad blah param." );
  }

  /*
    Examples from RFC 3261.
  */
  {
    std::string u( "sip:alice@atlanta.com" );
    sipuri s( u );
    test( s.protocol.substr( u ), "sip", "Bad proto." );
    test( s.user.substr( u ), "alice", "Bad user." );
    test( s.host.substr( u ), "atlanta.com", "Bad host." );
  }

  {
    std::string u( "sip:alice:secretword@atlanta.com;transport=tcp" );
    sipuri s( u );
    test( s.protocol.substr( u ), "sip", "Bad proto." );
    test( s.user.substr( u ), "alice", "Bad user." );
    test( s.secret.substr( u ), "secretword", "Bad secret." );
    test( s.host.substr( u ), "atlanta.com", "Bad host." );
    test( s.parameters.substr( u ), "transport=tcp", "Bad params." );
    test( s.getparameter( u, "transport" ).substr( u ), "tcp", "Bad transport param." );
  }

  {
    // TODO getting header
    std::string u( "sips:alice@atlanta.com?subject=project%20x&priority=urgent" );
    sipuri s( u );
    test( s.protocol.substr( u ), "sips", "Bad proto." );
    test( s.user.substr( u ), "alice", "Bad user." );
    test( s.host.substr( u ), "atlanta.com", "Bad host." );
    test( urldecode( s.headers.substr( u ) ), "subject=project x&priority=urgent", "Bad headers." );
  }

  {
    std::string u( "sip:+1-212-555-1212:1234@gateway.com;user=phone" );
    sipuri s( u );
    test( s.protocol.substr( u ), "sip", "Bad proto." );
    test( s.user.substr( u ), "+1-212-555-1212", "Bad user." );
    test( s.secret.substr( u ), "1234", "Bad secret." );
    test( s.host.substr( u ), "gateway.com", "Bad host." );
    test( s.parameters.substr( u ), "user=phone", "Bad params." );
    test( s.getparameter( u, "user" ).substr( u ), "phone", "Bad transport param." );
  }

  {
    std::string u( "sips:1212@gateway.com" );
    sipuri s( u );
    test( s.protocol.substr( u ), "sips", "Bad proto." );
    test( s.user.substr( u ), "1212", "Bad user." );
    test( s.host.substr( u ), "gateway.com", "Bad host." );
  }

  {
    std::string u( "sip:alice@192.0.2.4" );
    sipuri s( u );
    test( s.protocol.substr( u ), "sip", "Bad proto." );
    test( s.user.substr( u ), "alice", "Bad user." );
    test( s.host.substr( u ), "192.0.2.4", "Bad host." );
  }

  {
    std::string u( "sip:atlanta.com;method=REGISTER?to=alice%40atlanta.com" );
    sipuri s( u );
    test( s.protocol.substr( u ), "sip", "Bad proto." );
    test( s.host.substr( u ), "atlanta.com", "Bad host." );
    test( s.parameters.substr( u ), "method=REGISTER", "Bad params." );
    test( s.headers.substr( u ), "to=alice%40atlanta.com", "Bad headers." );
  }

  {
    std::string u( "param=the cat,the dog+" );
    test( urlencode( u ), "param%3dthe+cat%2cthe+dog%2b", "Url encode failed." );
  }

  std::cout << "All url class tests passed, looking good" << std::endl;
}

/*******************************************************************************
Function: main
Purpose: Run our tests
Updated: 12.12.2018
*******************************************************************************/
int main( int argc, const char* argv[] )
{
  testurl();
  testsippacket();

  return 0;
}




