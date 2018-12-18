


#ifndef PROJECTWEBDOCUMENT_H
#define PROJECTWEBDOCUMENT_H

#define MAX_HEADERS 20
#define MAX_DUPLICATEHEADERS 3
#define METHODUNKNOWN -1
#define METHODBADFORMAT -2
#define STATUSUNKNOWN -1

#include <array>
#include <string>

/*******************************************************************************
Function: moveonbyn
Purpose: Little helper function - safely move on n chars in a string.
Updated: 12.12.2018
*******************************************************************************/
inline std::string::iterator moveonbyn(std::string &s, size_t n)
{
  return s.length() <= n ? s.end() : s.begin() + n;
}

inline void moveonbyn(std::string &str, std::string::iterator &s, size_t n)
{
  for( size_t i = 0; i < n; i++ )
  {
    s++;
    if( str.end() == s )
    {
      return;
    }
  }
}

/*******************************************************************************
Class: substring
Purpose: Helper class, used to hold indexes into a larger string so that we
only need to maintain 1 buffer.
Updated: 12.12.2018
*******************************************************************************/
class substring
{
public:
  inline substring()
  {
    this->startpos = 0;
    this->endpos = 0;
  }

  inline substring( size_t start, size_t end )
  {
    this->startpos = start;
    this->endpos = end;
  }

  inline std::string substr( std::string &ref )
  {
    if( 0 == this->endpos )
    {
      return "";
    }

    if( this->startpos > ref.size() )
    {
      return "";
    }

    if( this->endpos > ref.size() )
    {
      return "";
    }
    return ref.substr( this->startpos, this->endpos - this->startpos );
  }

  inline size_t end()
  {
    return this->endpos;
  }
  inline size_t start()
  {
    return this->startpos;
  }

  inline size_t end( size_t end )
  {
    this->endpos = end;
    return this->endpos;
  }
  inline size_t start( size_t start )
  {
    this->startpos = start;
    return this->startpos;
  }

  inline size_t operator++( int )
  {
    return this->endpos ++;
  }

  inline size_t operator--( int )
  {
    return this->endpos --;
  }

  inline size_t length( void )
  {
    return this->endpos - this->startpos;
  }

private:
  size_t startpos;
  size_t endpos;
};

std::string urldecode( std::string str );
std::string urlencode( std::string str );

/*******************************************************************************
Class: httpuri
Purpose: Helper class to parse urls. Must be paired with the origional buffer
to retreive the actual string. TODO move this into our http file.
Updated: 17.12.2018
*******************************************************************************/
class httpuri
{
public:
  inline httpuri( std::string &s )
  {
    size_t protopos = s.find( "://" );
    if( std::string::npos != protopos )
    {
      this->protocol = substring( 0, protopos );
    }

    size_t hostpos = this->protocol.end();
    if( hostpos != 0 )
    {
      hostpos += 3;
    }

    hostpos = s.find( '/', hostpos );
    if( std::string::npos == hostpos )
    {
      hostpos = s.size();
    }
    this->host = substring( this->protocol.end() + 3, hostpos );
    if( this->host.end() == s.size() )
    {
      return;
    }

    size_t querypos = s.find( '?', this->host.end() );
    if( std::string::npos != querypos )
    {
      this->query = substring( querypos + 1, s.size() );
      /* The path is inbetween the host and query */
      this->path = substring( this->host.end(), querypos );
      return;
    }

    this->path = substring( this->host.end(), s.size() );

  }

  substring protocol;
  substring host;
  substring path;
  substring query;
};

/*******************************************************************************
Class: projectwebdocument
Purpose: Base class with common function to parse web documents. For example
SIP packets of HTTP requests - who both have a header line followed by headers
and values.
Updated: 16.12.2018
*******************************************************************************/
class projectwebdocument
{
public:
  projectwebdocument( std::string doc );
  std::string getheader( int header );
  bool hasheader( int header );
  int getmethod( void );
  int getstatuscode( void );
  std::string getreasonphrase( void );
  std::string getrequesturi( void );
  std::string getbody();

protected:

  virtual int getheaderfromcrc( int crc ) = 0;
  virtual void parsemethod( void ) = 0;

  void parseheaders( void );
  substring getheadervalue( substring header );

  std::string document;
  bool headersparsed;

  substring headers[ MAX_HEADERS * MAX_DUPLICATEHEADERS ];
  substring body;

  int method;
  substring methodstr;

  int statuscode;
  substring statuscodestr;
  substring reasonphrase;

private:
  void storeheader( int headerindex, substring hval );
};






#endif /* PROJECTWEBDOC_H */
