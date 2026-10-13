/* -*- C++ -*- */
 
/****************************************************************************
** Copyright (c) 2001-2014
**
** This file is part of the QuickFIX FIX Engine
**
** This file may be distributed under the terms of the quickfixengine.org
** license as defined by quickfixengine.org and appearing in the file
** LICENSE included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.quickfixengine.org/LICENSE for licensing information.
**
** Contact ask@quickfixengine.org if any conditions of this licensing are
** not clear to you.
**
****************************************************************************/

#ifndef FIXT11_MESSAGECRACKER_H
#define FIXT11_MESSAGECRACKER_H


#include "../SessionID.h"
#include "../Exceptions.h"
#include <string_view>

#include "../fixt11/Message.h"

namespace FIXT11
{  
  class Heartbeat; 
  class TestRequest; 
  class ResendRequest; 
  class Reject; 
  class SequenceReset; 
  class Logout; 
  class Logon; 
  class XMLnonFIX;

  class MessageCracker
  {
  public:
  virtual ~MessageCracker() {}
  virtual void onMessage( const Message&, const FIX::SessionID& )
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( Message&, const FIX::SessionID& )
    { throw FIX::UnsupportedMessageType(); }
 virtual void onMessage( const Heartbeat&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const TestRequest&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const ResendRequest&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const Reject&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const SequenceReset&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const Logout&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const Logon&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const XMLnonFIX&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( Heartbeat&, const FIX::SessionID& ) {} 
 virtual void onMessage( TestRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ResendRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( Reject&, const FIX::SessionID& ) {} 
 virtual void onMessage( SequenceReset&, const FIX::SessionID& ) {} 
 virtual void onMessage( Logout&, const FIX::SessionID& ) {} 
 virtual void onMessage( Logon&, const FIX::SessionID& ) {} 
 virtual void onMessage( XMLnonFIX&, const FIX::SessionID& ) {} 

public:
  // Dispatch switches on the MsgType's characters; every standard MsgType has one or two. A longer one
  // from a custom dictionary is compared as a string after the switch (#159).
  static constexpr unsigned msgTypeKey( std::string_view value )
  {
    return value.size() == 1 ? static_cast<unsigned char>( value[0] )
         : value.size() == 2 ? static_cast<unsigned char>( value[0] ) | ( static_cast<unsigned char>( value[1] ) << 8 )
         : 0;
  }

  void crack( const Message& message, 
              const FIX::SessionID& sessionID )
  {
    const std::string & msgTypeValue 
      = message.getHeader().getField( FIX::FIELD::MsgType );
    
    switch( msgTypeKey( msgTypeValue ) )
    {
    case msgTypeKey( "0" ):
      return onMessage( (const Heartbeat&)message, sessionID );
    case msgTypeKey( "1" ):
      return onMessage( (const TestRequest&)message, sessionID );
    case msgTypeKey( "2" ):
      return onMessage( (const ResendRequest&)message, sessionID );
    case msgTypeKey( "3" ):
      return onMessage( (const Reject&)message, sessionID );
    case msgTypeKey( "4" ):
      return onMessage( (const SequenceReset&)message, sessionID );
    case msgTypeKey( "5" ):
      return onMessage( (const Logout&)message, sessionID );
    case msgTypeKey( "A" ):
      return onMessage( (const Logon&)message, sessionID );
    case msgTypeKey( "n" ):
      return onMessage( (const XMLnonFIX&)message, sessionID );
    default:
      break;
    }
    
    return onMessage( message, sessionID );
  }
  
void crack( Message& message, 
            const FIX::SessionID& sessionID )
  {
    const std::string & msgTypeValue 
      = message.getHeader().getField( FIX::FIELD::MsgType );
    
    switch( msgTypeKey( msgTypeValue ) )
    {
    case msgTypeKey( "0" ):
      return onMessage( (Heartbeat&)message, sessionID );
    case msgTypeKey( "1" ):
      return onMessage( (TestRequest&)message, sessionID );
    case msgTypeKey( "2" ):
      return onMessage( (ResendRequest&)message, sessionID );
    case msgTypeKey( "3" ):
      return onMessage( (Reject&)message, sessionID );
    case msgTypeKey( "4" ):
      return onMessage( (SequenceReset&)message, sessionID );
    case msgTypeKey( "5" ):
      return onMessage( (Logout&)message, sessionID );
    case msgTypeKey( "A" ):
      return onMessage( (Logon&)message, sessionID );
    case msgTypeKey( "n" ):
      return onMessage( (XMLnonFIX&)message, sessionID );
    default:
      break;
    }
    
    return onMessage( message, sessionID );
  }

  };
}

#endif
