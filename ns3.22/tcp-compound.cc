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

#define NS_LOG_APPEND_CONTEXT \
  if (m_node) { std::clog << Simulator::Now ().GetSeconds () << " [node " << m_node->GetId () << "] "; }

#include "tcp-compound.h"
#include "ns3/log.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/simulator.h"
#include "ns3/abort.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include <math.h>

NS_LOG_COMPONENT_DEFINE ("TcpCompound");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (TcpCompound);

TypeId
TcpCompound::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpCompound")
    .SetParent<TcpSocketBase> ()
    .AddConstructor<TcpCompound> ()
    .AddAttribute ("ReTxThreshold", "Threshold for fast retransmit",
                    UintegerValue (3),
                    MakeUintegerAccessor (&TcpCompound::m_retxThresh),
                    MakeUintegerChecker<uint32_t> ())
    .AddTraceSource ("CongestionWindow",
                     "The TCP connection's congestion window",
                     MakeTraceSourceAccessor (&TcpCompound::m_cWnd))
    .AddTraceSource ("DelayWindow",
                     "The TCP compound connection's delay window",
                     MakeTraceSourceAccessor (&TcpCompound::m_dWnd))
    .AddTraceSource ("Window",
                     "The TCP compound connection's window",
                     MakeTraceSourceAccessor (&TcpCompound::m_Wnd))
    .AddTraceSource ("SlowStartThreshold",
                     "TCP slow start threshold (bytes)",
                     MakeTraceSourceAccessor (&TcpCompound::m_ssThresh))
  ;
  return tid;
}

TcpCompound::TcpCompound (void) :
		m_retxThresh (3),
		m_inFastRec (false),
		// Added by Fan Zhou
		m_dWnd (0),
		m_Wnd (0),
		m_lastWindow (0),
		m_enableThresh (38),
		m_begSndUna (0),
		m_begSndNxt (0),
		m_baseRtt (Seconds(1000)),
		m_smoothedRtt (0),
		m_alpha (0.125),
		m_beta (0.5),
		m_gamma (30),
		m_zeta (0.03),
		m_k (0.75) ,
		m_i(0)
		//justfortest (0)

{
  NS_LOG_FUNCTION (this);
}

TcpCompound::TcpCompound (const TcpCompound& sock)
  : TcpSocketBase (sock),
    m_cWnd (sock.m_cWnd),
    m_ssThresh (sock.m_ssThresh),
    m_initialCWnd (sock.m_initialCWnd),
    m_initialSsThresh (sock.m_initialSsThresh),
    m_retxThresh (sock.m_retxThresh),
    m_inFastRec (false),
	// Added by Fan Zhou
	m_dWnd (sock.m_dWnd),
	m_lastWindow (sock.m_lastWindow),
	m_enableThresh (sock.m_enableThresh),
	m_begSndUna (sock.m_begSndUna),
	m_begSndNxt (sock.m_begSndNxt),
	m_baseRtt (sock.m_baseRtt),
	m_smoothedRtt (sock.m_smoothedRtt),
	m_alpha (sock.m_alpha),
	m_beta (sock.m_beta),
	m_gamma (sock.m_gamma),
	m_zeta (sock.m_zeta),
	m_k (sock.m_k)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor");
}

TcpCompound::~TcpCompound (void)
{
}

/* We initialize m_cWnd from this function, after attributes initialized */
int
TcpCompound::Listen (void)
{
  NS_LOG_FUNCTION (this);
  InitializeCwnd ();
  return TcpSocketBase::Listen ();
}

/* We initialize m_cWnd from this function, after attributes initialized */
int
TcpCompound::Connect (const Address & address)
{
  NS_LOG_FUNCTION (this << address);
  InitializeCwnd ();
  return TcpSocketBase::Connect (address);
}

/* Limit the size of in-flight data by cwnd and receiver's rxwin */
uint32_t
TcpCompound::Window (void)
{
  NS_LOG_FUNCTION (this);
  m_Wnd = m_cWnd.Get () + m_dWnd.Get ();
  //NS_LOG_UNCOND ("cwnd=" << m_cWnd.Get () << ", dwnd=" << m_dWnd.Get () << " , m_Wnd=" <<m_Wnd.Get());
  return std::min (m_rWnd.Get (), m_Wnd.Get ());
  //return  (m_cWnd.Get () + m_dWnd.Get ());
}

Ptr<TcpSocketBase>
TcpCompound::Fork (void)
{
  return CopyObject<TcpCompound> (this);
}

/* Added by Fan Zhou, every time a new ACK or a duplicate ACK is received, calculate the base and smoothed RTT*/
void
TcpCompound::ReceivedAck (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this);

  if ((0 != (tcpHeader.GetFlags () & TcpHeader::ACK)))
  {
	  // Only do it for ACK
	  // Calculate m_lastRtt
	  TcpSocketBase::EstimateRtt (tcpHeader);
	  // Update base RTT
	  UpdateBaseRtt();
	  // Get the smoothed RTT
	  Filtering();
  }
  TcpSocketBase::ReceivedAck (packet, tcpHeader);
}


/* New ACK (up to seqnum seq) received. Increase cwnd and call TcpSocketBase::NewAck() */
void
TcpCompound::NewAck (const SequenceNumber32& seq)
{
  NS_LOG_FUNCTION (this << seq);

  NS_LOG_LOGIC ("TcpCompound receieved ACK for seq " << seq <<
                " cwnd " << m_cWnd <<
                " ssthresh " << m_ssThresh);
  // Check for exit condition of fast recovery
  if (m_inFastRec)
    { // RFC2001, sec.4; RFC2581, sec.3.2
      // First new ACK after fast recovery: reset cwnd
      m_cWnd = m_ssThresh;
      // Added by Fan Zhou, after we end fast recovery, dwnd should be set up to appropriate value
      double adderDwnd = (double)m_lastWindow * (1 - m_beta);
      m_dWnd = (uint32_t)std::max(0.0, adderDwnd - (double)m_ssThresh.Get());
      NS_LOG_INFO ("Stop fast recovery, TCP Compound: updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh);
      m_inFastRec = false;
      ResetCompound();
    };

  // Increase of cwnd based on current phase (slow start or congestion avoidance)
  if (m_cWnd < m_ssThresh)
    { // Slow start mode, add one segSize to cWnd. Default m_ssThresh is 65535. (RFC2001, sec.1)
      m_cWnd += m_segmentSize;
      NS_LOG_INFO ("In SlowStart, TCP Compound: updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh);
    }
  else
    {
	  // Added by Fan Zhou
	  // Congestion avoidance mode, if cwnd < m_enableThresh, increase as in Reno, else increase as in TCP Compound
	  if ((m_cWnd.Get() / m_segmentSize) < m_enableThresh)
	  {
		  double adder = static_cast<double> (m_segmentSize * m_segmentSize) / m_cWnd.Get();
		  adder = std::max(1.0, adder);
		  m_cWnd += static_cast<uint32_t>(adder);
		  NS_LOG_INFO ("In Normal CongAvoid, (cwnd is " << m_cWnd.Get() / m_segmentSize << "packets), updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh);
	  }
	  else
	  {
		  // Enter into compound congestion avoidence mode
		  if (m_begSndUna == SequenceNumber32(0))
		  {
		      // For new ACK, the m_begSndUna is 0, we should set it to the seq number of next sent packet
			  m_begSndUna = m_nextTxSequence;
			  NS_LOG_INFO("seq: " << seq << " Restarting Compound  "  << " m_begSndUna: " << m_begSndUna);
			  m_lastWindow = m_Wnd.Get();
		  }
		  double adderCwnd = static_cast<double> (m_segmentSize * m_segmentSize) / m_Wnd.Get();
		  adderCwnd = std::max(1.0, adderCwnd);
		  m_cWnd += static_cast<uint32_t>(adderCwnd);
		  // When the packet has been ACKed, a new RTT ends
		  if (seq.GetValue() == (m_begSndUna.GetValue() + m_segmentSize))
		  {
			  m_begSndNxt = m_nextTxSequence;
			  uint32_t actualPackets = (m_begSndNxt.GetValue() - m_begSndUna.GetValue()) / m_segmentSize; // Packets transmitted during last RTT
			  NS_LOG_INFO("A new RTT ended" << " m_begSndUna: " << m_begSndUna << " m_begSndNxt: " << m_begSndNxt << " m_lastWindow: " << m_lastWindow);
			  // Calculating diff using formula: (expected throughput - actual throughput) * baseRtt
			  // Expected throughput = win/(MSS * baseRtt); Actual throughput = actual packets sent during last RTT/last RTT
			  // RFC draft-sridharan-tcpm-ctcp-00
			  double expectedThu = (double)m_Wnd.Get()/((double)m_segmentSize * m_baseRtt.ToDouble(Time::S)); // Expected throughput: packets transmitted in last RTT/base RTT
			  double actualThu = (double)actualPackets/m_lastRtt.Get().ToDouble(Time::S); // Actual throughput: packets transmitted in last RTT/smoothed RTT
			  double diff = (expectedThu - actualThu)*m_baseRtt.ToDouble(Time::S);
			  double adderDwnd;
			  //NS_LOG_UNCOND("m_lastRtt: " << m_lastRtt << " ExpectedThu: " << expectedThu << " Actual Thu " << actualThu << " Diff: " << diff);
			  if (diff < m_gamma)
			  {
				  adderDwnd = (m_alpha*pow((double)(m_lastWindow/m_segmentSize), m_k) - 1) * (double)m_segmentSize;
				  adderDwnd = std::max(0.0, adderDwnd);
			  }
			  else
			  {
				  adderDwnd = -m_zeta*diff*m_segmentSize;
			  }
			  m_dWnd = (uint32_t)std::max(0.0, (double)m_dWnd.Get() + adderDwnd);
			  NS_LOG_INFO("diff: " << diff << " m_lastWindow " << m_lastWindow <<
					  " adderDwnd " << adderDwnd << " m_dWnd: " << m_dWnd);

			  m_begSndUna = m_begSndNxt;
			  m_lastWindow = m_cWnd + m_dWnd;
			  NS_LOG_INFO ("In CongAvoid, TCP Compound: updated to cwnd; " << m_cWnd << " updated to dwnd " << m_dWnd);
		  }
	  }
    }
  // Complete newAck processing
  TcpSocketBase::NewAck (seq);
}

// Fast recovery and fast retransmit
void
TcpCompound::DupAck (const TcpHeader& tcpHeader, uint32_t count)
{
  NS_LOG_FUNCTION (this << "tcpHeader " << count);
  if (count == m_retxThresh && !m_inFastRec)
    { // triple duplicate ack triggers fast retransmit (RFC2581, sec.3.2)
      m_ssThresh = std::max (2 * m_segmentSize, BytesInFlight () / 2);
      NS_LOG_INFO ("Enter fast recovery, TCP Compound: updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh);
      m_cWnd = m_ssThresh + 3 * m_segmentSize;
      m_inFastRec = true;
      NS_LOG_INFO ("Triple dupack. Reset cwnd to " << m_cWnd << ", ssthresh to " << m_ssThresh);
      DoRetransmit ();
    }
  else if (m_inFastRec)
    { // In fast recovery, inc cwnd for every additional dupack (RFC2581, sec.3.2)
      m_cWnd += m_segmentSize;
      NS_LOG_INFO ("Increased cwnd to " << m_cWnd);
      SendPendingData (m_connected);
    };
}

// Retransmit timeout
void TcpCompound::Retransmit (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  ResetCompound();
  m_inFastRec = false;
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT) return;
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= ESTABLISHED && m_txBuffer->HeadSequence () >= m_highTxMark) return;

  // According to RFC2581 sec.3.1, upon RTO, ssthresh is set to half of flight
  // size and cwnd is set to 1*MSS, then the lost packet is retransmitted and
  // TCP back to slow start
  m_ssThresh = std::max (2 * m_segmentSize, BytesInFlight () / 2);
  //m_ssThresh = std::max (2 * m_segmentSize, m_cWnd.Get() / 2);
  m_cWnd = m_segmentSize;
  m_nextTxSequence = m_txBuffer->HeadSequence (); // Restart from highest Ack
  NS_LOG_INFO ("RTO. Reset cwnd to " << m_cWnd <<
               ", ssthresh to " << m_ssThresh << ", restart from seqnum " << m_nextTxSequence);
  DoRetransmit ();                          // Retransmit the packet
}


// Added by Fan Zhou
void
TcpCompound::UpdateBaseRtt ()
{
  NS_LOG_FUNCTION (this);
  // Update minRtt
  if (m_lastRtt != Time (0))
  {
      if (m_lastRtt.Get().ToDouble(Time::NS) < m_baseRtt.ToDouble(Time::NS))
      {
          m_baseRtt = m_lastRtt.Get();
      }
  }
}


// Added by Fan Zhou
void
TcpCompound::Filtering ()
{
  NS_LOG_FUNCTION (this);
  double smooth = 0.9;
  if (m_smoothedRtt == Time(0))
  {
	  m_smoothedRtt = m_lastRtt;
  }
  m_smoothedRtt = Time::FromDouble ((smooth * m_lastRtt.Get().ToDouble(Time::NS)) +
		  (1 - smooth) * m_smoothedRtt.ToDouble(Time::NS), Time::NS);
}


// Added by Fan Zhou
void
TcpCompound::ResetCompound()
{
	m_dWnd = 0; 								 // If a loss is detected, reset m_dwnd to 0
	m_begSndUna = SequenceNumber32(0);
	m_begSndNxt = SequenceNumber32(0);
	m_lastWindow = 0;
}

void
TcpCompound::SetSegSize (uint32_t size)
{
  NS_ABORT_MSG_UNLESS (m_state == CLOSED, "TcpCompound::SetSegSize() cannot change segment size after connection started.");
  m_segmentSize = size;
}

void
TcpCompound::SetInitialSSThresh (uint32_t threshold)
{
  NS_ABORT_MSG_UNLESS (m_state == CLOSED, "TcpCompound::SetSSThresh() cannot change initial ssThresh after connection started.");
  m_initialSsThresh = threshold;
}

uint32_t
TcpCompound::GetInitialSSThresh (void) const
{
  return m_initialSsThresh;
}

void
TcpCompound::SetInitialCwnd (uint32_t cwnd)
{
  NS_ABORT_MSG_UNLESS (m_state == CLOSED, "TcpCompound::SetInitialCwnd() cannot change initial cwnd after connection started.");
  m_initialCWnd = cwnd;
}

uint32_t
TcpCompound::GetInitialCwnd (void) const
{
  return m_initialCWnd;
}

void
TcpCompound::InitializeCwnd (void)
{
  /*
   * Initialize congestion window, default to 1 MSS (RFC2001, sec.1) and must
   * not be larger than 2 MSS (RFC2581, sec.3.1). Both m_initiaCWnd and
   * m_segmentSize are set by the attribute system in ns3::TcpSocket.
   */
  m_cWnd = m_initialCWnd * m_segmentSize;
  m_ssThresh = m_initialSsThresh;
}

} // namespace ns3
