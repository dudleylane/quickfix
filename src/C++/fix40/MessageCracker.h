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

#ifndef FIX40_MESSAGECRACKER_H
#define FIX40_MESSAGECRACKER_H


#include "../SessionID.h"
#include "../Exceptions.h"
#include <string_view>

#include "../fix40/Message.h"

namespace FIX40
{  
  class Heartbeat; 
  class TestRequest; 
  class ResendRequest; 
  class Reject; 
  class SequenceReset; 
  class Logout; 
  class IOI; 
  class Advertisement; 
  class ExecutionReport; 
  class OrderCancelReject; 
  class Logon; 
  class News; 
  class Email; 
  class NewOrderSingle; 
  class NewOrderList; 
  class OrderCancelRequest; 
  class OrderCancelReplaceRequest; 
  class OrderStatusRequest; 
  class Allocation; 
  class ListCancelRequest; 
  class ListExecute; 
  class ListStatusRequest; 
  class ListStatus; 
  class AllocationInstructionAck; 
  class DontKnowTrade; 
  class QuoteRequest; 
  class Quote;

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
  virtual void onMessage( const IOI&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Advertisement&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ExecutionReport&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderCancelReject&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Logon&, const FIX::SessionID& ) 
    {}
  virtual void onMessage( const News&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Email&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const NewOrderSingle&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const NewOrderList&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderCancelRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderCancelReplaceRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const OrderStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Allocation&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ListCancelRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ListExecute&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ListStatusRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const ListStatus&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const AllocationInstructionAck&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const DontKnowTrade&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const QuoteRequest&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( const Quote&, const FIX::SessionID& ) 
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( Heartbeat&, const FIX::SessionID& ) {} 
 virtual void onMessage( TestRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ResendRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( Reject&, const FIX::SessionID& ) {} 
 virtual void onMessage( SequenceReset&, const FIX::SessionID& ) {} 
 virtual void onMessage( Logout&, const FIX::SessionID& ) {} 
 virtual void onMessage( IOI&, const FIX::SessionID& ) {} 
 virtual void onMessage( Advertisement&, const FIX::SessionID& ) {} 
 virtual void onMessage( ExecutionReport&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderCancelReject&, const FIX::SessionID& ) {} 
 virtual void onMessage( Logon&, const FIX::SessionID& ) {} 
 virtual void onMessage( News&, const FIX::SessionID& ) {} 
 virtual void onMessage( Email&, const FIX::SessionID& ) {} 
 virtual void onMessage( NewOrderSingle&, const FIX::SessionID& ) {} 
 virtual void onMessage( NewOrderList&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderCancelRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderCancelReplaceRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( OrderStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( Allocation&, const FIX::SessionID& ) {} 
 virtual void onMessage( ListCancelRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ListExecute&, const FIX::SessionID& ) {} 
 virtual void onMessage( ListStatusRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( ListStatus&, const FIX::SessionID& ) {} 
 virtual void onMessage( AllocationInstructionAck&, const FIX::SessionID& ) {} 
 virtual void onMessage( DontKnowTrade&, const FIX::SessionID& ) {} 
 virtual void onMessage( QuoteRequest&, const FIX::SessionID& ) {} 
 virtual void onMessage( Quote&, const FIX::SessionID& ) {} 

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
    case msgTypeKey( "6" ):
      return onMessage( (const IOI&)message, sessionID );
    case msgTypeKey( "7" ):
      return onMessage( (const Advertisement&)message, sessionID );
    case msgTypeKey( "8" ):
      return onMessage( (const ExecutionReport&)message, sessionID );
    case msgTypeKey( "9" ):
      return onMessage( (const OrderCancelReject&)message, sessionID );
    case msgTypeKey( "A" ):
      return onMessage( (const Logon&)message, sessionID );
    case msgTypeKey( "B" ):
      return onMessage( (const News&)message, sessionID );
    case msgTypeKey( "C" ):
      return onMessage( (const Email&)message, sessionID );
    case msgTypeKey( "D" ):
      return onMessage( (const NewOrderSingle&)message, sessionID );
    case msgTypeKey( "E" ):
      return onMessage( (const NewOrderList&)message, sessionID );
    case msgTypeKey( "F" ):
      return onMessage( (const OrderCancelRequest&)message, sessionID );
    case msgTypeKey( "G" ):
      return onMessage( (const OrderCancelReplaceRequest&)message, sessionID );
    case msgTypeKey( "H" ):
      return onMessage( (const OrderStatusRequest&)message, sessionID );
    case msgTypeKey( "J" ):
      return onMessage( (const Allocation&)message, sessionID );
    case msgTypeKey( "K" ):
      return onMessage( (const ListCancelRequest&)message, sessionID );
    case msgTypeKey( "L" ):
      return onMessage( (const ListExecute&)message, sessionID );
    case msgTypeKey( "M" ):
      return onMessage( (const ListStatusRequest&)message, sessionID );
    case msgTypeKey( "N" ):
      return onMessage( (const ListStatus&)message, sessionID );
    case msgTypeKey( "P" ):
      return onMessage( (const AllocationInstructionAck&)message, sessionID );
    case msgTypeKey( "Q" ):
      return onMessage( (const DontKnowTrade&)message, sessionID );
    case msgTypeKey( "R" ):
      return onMessage( (const QuoteRequest&)message, sessionID );
    case msgTypeKey( "S" ):
      return onMessage( (const Quote&)message, sessionID );
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
    case msgTypeKey( "6" ):
      return onMessage( (IOI&)message, sessionID );
    case msgTypeKey( "7" ):
      return onMessage( (Advertisement&)message, sessionID );
    case msgTypeKey( "8" ):
      return onMessage( (ExecutionReport&)message, sessionID );
    case msgTypeKey( "9" ):
      return onMessage( (OrderCancelReject&)message, sessionID );
    case msgTypeKey( "A" ):
      return onMessage( (Logon&)message, sessionID );
    case msgTypeKey( "B" ):
      return onMessage( (News&)message, sessionID );
    case msgTypeKey( "C" ):
      return onMessage( (Email&)message, sessionID );
    case msgTypeKey( "D" ):
      return onMessage( (NewOrderSingle&)message, sessionID );
    case msgTypeKey( "E" ):
      return onMessage( (NewOrderList&)message, sessionID );
    case msgTypeKey( "F" ):
      return onMessage( (OrderCancelRequest&)message, sessionID );
    case msgTypeKey( "G" ):
      return onMessage( (OrderCancelReplaceRequest&)message, sessionID );
    case msgTypeKey( "H" ):
      return onMessage( (OrderStatusRequest&)message, sessionID );
    case msgTypeKey( "J" ):
      return onMessage( (Allocation&)message, sessionID );
    case msgTypeKey( "K" ):
      return onMessage( (ListCancelRequest&)message, sessionID );
    case msgTypeKey( "L" ):
      return onMessage( (ListExecute&)message, sessionID );
    case msgTypeKey( "M" ):
      return onMessage( (ListStatusRequest&)message, sessionID );
    case msgTypeKey( "N" ):
      return onMessage( (ListStatus&)message, sessionID );
    case msgTypeKey( "P" ):
      return onMessage( (AllocationInstructionAck&)message, sessionID );
    case msgTypeKey( "Q" ):
      return onMessage( (DontKnowTrade&)message, sessionID );
    case msgTypeKey( "R" ):
      return onMessage( (QuoteRequest&)message, sessionID );
    case msgTypeKey( "S" ):
      return onMessage( (Quote&)message, sessionID );
    default:
      break;
    }
    
    return onMessage( message, sessionID );
  }

  };
}

#endif
