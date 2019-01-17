
#ifndef PROJECTSIPSERVER_H
#define PROJECTSIPSERVER_H

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio/ip/udp.hpp>

#include "projectsippacket.h"


/*******************************************************************************
Class: projectsipserver
Purpose: Our simple UDP SIP server
Updated: 12.12.2018
*******************************************************************************/
class projectsipserver
{

public:
  projectsipserver( boost::asio::io_service &io_service, short port );
  boost::asio::ip::udp::socket *getsocket( void ){ return &this->socket; }

private:
  boost::asio::ip::udp::socket socket;
  boost::asio::ip::udp::endpoint sender_endpoint;
  enum { max_length = 1500 };
  char data[ max_length ];
  int bytesreceived;

  void handlereadsome( void );
  void handledata( void );
};


/*******************************************************************************
Class: projectsipserverpacket
Purpose: Used to parse SIP packets - but allows us to send the response to
the correct place.
Updated: 03.01.2019
*******************************************************************************/
class projectsipserverpacket : public projectsippacket
{
public:
  projectsipserverpacket( projectsipserver *sipserver, boost::asio::ip::udp::endpoint sender_endpoint, stringptr pk ) :
    projectsippacket( pk )
  {
  	this->sender_endpoint = sender_endpoint;
  	this->sipserver = sipserver;
  }

  virtual void respond( stringptr doc );

  void handlesend( boost::shared_ptr<std::string>,
      const boost::system::error_code&,
      std::size_t );

private:
  projectsipserver *sipserver;
  boost::asio::ip::udp::endpoint sender_endpoint;
};


#endif /* PROJECTSIPSERVER_H */