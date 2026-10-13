<!--
*****************************************************************************
  Copyright (c) 2001-2014

  This file is part of the QuickFIX FIX Engine

  This file may be distributed under the terms of the quickfixengine.org
  license as defined by quickfixengine.org and appearing in the file
  LICENSE included in the packaging of this file.

  This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
  WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

  See http://www.quickfixengine.org/LICENSE for licensing information.

  Contact ask@quickfixengine.org if any conditions of this licensing are
  not clear to you.
*****************************************************************************
-->

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
 <xsl:output  method="text" encoding="UTF-8"/>

 <xsl:template match="text()"/>

 <xsl:variable name="lowercase" select="'abcdefghijklmnopqrstuvwxyz'" />
 <xsl:variable name="uppercase" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'" />
 <xsl:variable name="type" select="//fix/@type"/>
 <xsl:variable name="lowertype" select="translate($type, $uppercase, $lowercase)"/>

 <xsl:template match="/">/* -*- C++ -*- */
 <xsl:copy-of select="document('COPYRIGHT.xml')"/>
<xsl:choose>
<xsl:when test="//fix/@servicepack='0'">
#ifndef <xsl:value-of select="$type"/><xsl:value-of select="//fix/@major"/><xsl:value-of select="//fix/@minor"/>_MESSAGECRACKER_H
#define <xsl:value-of select="$type"/><xsl:value-of select="//fix/@major"/><xsl:value-of select="//fix/@minor"/>_MESSAGECRACKER_H
</xsl:when>
<xsl:otherwise>
#ifndef <xsl:value-of select="$type"/><xsl:value-of select="//fix/@major"/><xsl:value-of select="//fix/@minor"/>SP<xsl:value-of select="//fix/@servicepack"/>_MESSAGECRACKER_H
#define <xsl:value-of select="$type"/><xsl:value-of select="//fix/@major"/><xsl:value-of select="//fix/@minor"/>SP<xsl:value-of select="//fix/@servicepack"/>_MESSAGECRACKER_H
</xsl:otherwise>
</xsl:choose>

#include "../SessionID.h"
#include "../Exceptions.h"
#include &lt;string_view&gt;
<xsl:choose>
<xsl:when test="//fix/@servicepack='0'">
#include "../<xsl:value-of select="$lowertype"/><xsl:value-of select="//fix/@major"/><xsl:value-of select="//fix/@minor"/>/Message.h"
</xsl:when>
<xsl:otherwise>
#include "../<xsl:value-of select="$lowertype"/><xsl:value-of select="//fix/@major"/><xsl:value-of select="//fix/@minor"/>sp<xsl:value-of select="//fix/@servicepack"/>/Message.h"
</xsl:otherwise>
</xsl:choose>

<xsl:choose>
<xsl:when test="//fix/@servicepack='0'">
namespace <xsl:value-of select="$type"/><xsl:value-of select="//fix/@major"/><xsl:value-of select="//fix/@minor"/>
</xsl:when>
<xsl:otherwise>
namespace <xsl:value-of select="$type"/><xsl:value-of select="//fix/@major"/><xsl:value-of select="//fix/@minor"/>SP<xsl:value-of select="//fix/@servicepack"/>
</xsl:otherwise>
</xsl:choose>
{ <xsl:call-template name="forward-declarations"/>

  class MessageCracker
  {
  public:
  virtual ~MessageCracker() {}
  virtual void onMessage( const Message&amp;, const FIX::SessionID&amp; )
    { throw FIX::UnsupportedMessageType(); }
  virtual void onMessage( Message&amp;, const FIX::SessionID&amp; )
    { throw FIX::UnsupportedMessageType(); }
<xsl:call-template name="virtual-const-functions"/>
<xsl:call-template name="virtual-functions"/>
<xsl:call-template name="switch-statement"/>
  };
}

#endif
</xsl:template>

<xsl:template name="forward-declarations"><xsl:for-each select="//fix/messages/message"> 
  class <xsl:value-of select="@name"/>;</xsl:for-each>
</xsl:template>

<xsl:template name="virtual-const-functions">
 <xsl:for-each select="//fix/messages/message"> virtual void onMessage( const <xsl:value-of select="@name"/>&amp;, const FIX::SessionID&amp; ) 
 <xsl:if test="@msgcat='app' and @name!='BusinessMessageReject'">   { throw FIX::UnsupportedMessageType(); }
 </xsl:if>
 <xsl:if test="@msgcat='app' and @name='BusinessMessageReject'">   {}
 </xsl:if>
 <xsl:if test="@msgcat='admin'">   {}
 </xsl:if>
</xsl:for-each>
</xsl:template>

<xsl:template name="virtual-functions">
 <xsl:for-each select="//fix/messages/message"> virtual void onMessage( <xsl:value-of select="@name"/>&amp;, const FIX::SessionID&amp; ) {} 
</xsl:for-each>
</xsl:template>

<xsl:template name="switch-statement">
public:
  // Dispatch switches on the MsgType's characters; every standard MsgType has one or two. A longer one
  // from a custom dictionary is compared as a string after the switch (#159).
  static constexpr unsigned msgTypeKey( std::string_view value )
  {
    return value.size() == 1 ? static_cast&lt;unsigned char&gt;( value[0] )
         : value.size() == 2 ? static_cast&lt;unsigned char&gt;( value[0] ) | ( static_cast&lt;unsigned char&gt;( value[1] ) &lt;&lt; 8 )
         : 0;
  }

  void crack( const Message&amp; message, 
              const FIX::SessionID&amp; sessionID )
  {
    const std::string &amp; msgTypeValue 
      = message.getHeader().getField( FIX::FIELD::MsgType );
    
    switch( msgTypeKey( msgTypeValue ) )
    {<xsl:for-each select="//fix/messages/message[string-length(@msgtype) &lt;= 2]">
    case msgTypeKey( "<xsl:value-of select="@msgtype"/>" ):
      return onMessage( (const <xsl:value-of select="@name"/>&amp;)message, sessionID );</xsl:for-each>
    default:
      break;
    }
    <xsl:for-each select="//fix/messages/message[string-length(@msgtype) &gt; 2]">
    if( msgTypeValue == "<xsl:value-of select="@msgtype"/>" )
      return onMessage( (const <xsl:value-of select="@name"/>&amp;)message, sessionID );
    </xsl:for-each>
    return onMessage( message, sessionID );
  }
  
void crack( Message&amp; message, 
            const FIX::SessionID&amp; sessionID )
  {
    const std::string &amp; msgTypeValue 
      = message.getHeader().getField( FIX::FIELD::MsgType );
    
    switch( msgTypeKey( msgTypeValue ) )
    {<xsl:for-each select="//fix/messages/message[string-length(@msgtype) &lt;= 2]">
    case msgTypeKey( "<xsl:value-of select="@msgtype"/>" ):
      return onMessage( (<xsl:value-of select="@name"/>&amp;)message, sessionID );</xsl:for-each>
    default:
      break;
    }
    <xsl:for-each select="//fix/messages/message[string-length(@msgtype) &gt; 2]">
    if( msgTypeValue == "<xsl:value-of select="@msgtype"/>" )
      return onMessage( (<xsl:value-of select="@name"/>&amp;)message, sessionID );
    </xsl:for-each>
    return onMessage( message, sessionID );
  }
</xsl:template>

</xsl:stylesheet>
