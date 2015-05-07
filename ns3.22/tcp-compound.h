/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 Fan Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Fan Zhou <charleszhou327@gmail.com>
 */

#ifndef TCP_COMPOUND_H
#define TCP_COMPOUND_H

#include "tcp-socket-base.h"
#include "ns3/packet.h"

namespace ns3 {

/**
 * \ingroup socket
 * \ingroup tcp
 *
 * \brief An implementation of a stream socket using TCP.
 *
 * This class contains the Compound implementation of TCP, according to \RFC{2581},
 * except sec.4.1 "re-starting idle connections", which we do not detect for
 * idleness and thus no slow start upon resumption.
 */
class TcpCompound : public TcpSocketBase
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  /**
   * Create an unbound tcp socket.
   */
  TcpCompound (void);
  /**
   * \brief Copy constructor
   * \param sock the object to copy
   */
  TcpCompound (const TcpCompound& sock);
  virtual ~TcpCompound (void);

  // From TcpSocketBase
  virtual int Connect (const Address &address);
  virtual int Listen (void);

protected:
  virtual uint32_t Window (void); // Return the max possible number of unacked bytes
  virtual Ptr<TcpSocketBase> Fork (void); // Call CopyObject<TcpReno> to clone me
  virtual void NewAck (const SequenceNumber32& seq); // Inc cwnd and call NewAck() of parent
  virtual void DupAck (const TcpHeader& t, uint32_t count);  // Fast retransmit
  virtual void Retransmit (void); // Retransmit timeout
  // Added by Fan Zhou
  virtual void ReceivedAck (Ptr<Packet> packet, const TcpHeader& tcpHeader); // Process received ACK

  //virtual void JustForTest (void);

  // Implementing ns3::TcpSocket -- Attribute get/set
  virtual void     SetSegSize (uint32_t size);
  virtual void     SetInitialSSThresh (uint32_t threshold);
  virtual uint32_t GetInitialSSThresh (void) const;
  virtual void     SetInitialCwnd (uint32_t cwnd);
  virtual uint32_t GetInitialCwnd (void) const;

  /**
   * \brief Estimate the RTT, record the minimum value
   * \param header the packet header
   */
  virtual void UpdateBaseRtt (void);

  /**
   * \brief Filtering the RTT to get exponential smoothed RTT
   */
  virtual void Filtering (void);

  /**
     * \brief Reset value of parameters used in TCP Compound
     */
  virtual void ResetCompound (void);

private:
  /**
   * \brief Set the congestion window when connection starts
   */
  void InitializeCwnd (void);

protected:
  TracedValue<uint32_t>  m_cWnd;         //!< Congestion window
  TracedValue<uint32_t>  m_ssThresh;     //!< Slow Start Threshold
  uint32_t               m_initialCWnd;  //!< Initial cWnd value
  uint32_t               m_initialSsThresh;  //!< Initial Slow Start Threshold value
  uint32_t               m_retxThresh;   //!< Fast Retransmit threshold
  bool                   m_inFastRec;    //!< currently in fast recovery

  // Added by Fan Zhou
  TracedValue<uint32_t>	 m_dWnd;		 // !< Delay based congestion window
  TracedValue<uint32_t>	 m_Wnd;		     // !< Delay based congestion window + congestion window
  uint32_t				 m_lastWindow;	 // !< The window size at the start point of last RTT
  uint32_t				 m_enableThresh; // !< Only enable dwnd if cwnd > m_enableThresh
  SequenceNumber32		 m_bytesNextSent; // !< Let us try it
  SequenceNumber32		 m_begSndUna; 	 //!< Left edge of the window at the beginning of last TCP Compound cycle.
  SequenceNumber32		 m_begSndNxt;	 //1< Right edge of the window at the beginning of last TCP Compound cycle.
  Time					 m_baseRtt;		 //!< Minimum RTT
  Time				 	 m_smoothedRtt;	 //!< Smoothed RTT
  double				 m_alpha;		 //!< Parameters used in TCP Compound protocol
  double				 m_beta;		 //!< Parameters used in TCP Compound protocol
  double				 m_gamma;		 //!< Parameters used in TCP Compound protocol
  double				 m_zeta;		 //!< Parameters used in TCP Compound protocol
  double				 m_k;			 //!< Parameters used in TCP Compound protocol
  double				 m_i;			 //!< Parameters used in TCP Compound protocol


  //uint32_t				 justfortest;

};

} // namespace ns3

#endif /* TCP_COMPOUND_H */
