/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
/*
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
 * Authors: Yaogong Wang <ywang15@ncsu.edu>
 */

#include "ndn-shaper-net-device-face.h"
#include "ndn-l3-protocol.h"

#include "ns3/net-device.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/node.h"
#include "ns3/pointer.h"
#include "ns3/random-variable.h"

#include "ns3/point-to-point-net-device.h"
#include "ns3/channel.h"
#include "ns3/ndn-name-components.h"
#include "ns3/ndn-header-helper.h"
#include "ns3/ndn-interest.h"
#include "ns3/simulator.h"

NS_LOG_COMPONENT_DEFINE ("ndn.ShaperNetDeviceFace");

namespace ns3 {
namespace ndn {

class TimestampTag : public Tag
{
public:
  TimestampTag (): m_timestamp (Simulator::Now ()) {}

  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::ndn::TimestampTag")
      .SetParent<Tag> ()
      .AddConstructor<TimestampTag> ()
    ;
    return tid;
  }

  virtual TypeId GetInstanceTypeId (void) const
  {
    return GetTypeId ();
  }

  virtual uint32_t GetSerializedSize (void) const
  {
    return 8;
  }

  virtual void Serialize (TagBuffer i) const
  {
    i.WriteU64 (m_timestamp.GetTimeStep());
  }

  virtual void Deserialize (TagBuffer i)
  {
    m_timestamp = Time(i.ReadU64 ());
  }

  virtual void Print (std::ostream &os) const
  {
    os << "Timestamp=" << m_timestamp;
  }

  Time GetTimestamp (void) const
  {
    return m_timestamp;
  }

private:
  Time m_timestamp;
};


NS_OBJECT_ENSURE_REGISTERED (ShaperNetDeviceFace);

TypeId
ShaperNetDeviceFace::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::ndn::ShaperNetDeviceFace")
    .SetParent<NetDeviceFace> ()
    .SetGroupName ("Ndn")
    .AddAttribute ("MaxInterest",
                   "Size of the shaper interest queue.",
                   UintegerValue (100),
                   MakeUintegerAccessor (&ShaperNetDeviceFace::m_maxInterest),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Headroom",
                   "Headroom in interest shaping to absorb burstiness.",
                   DoubleValue (0.98),
                   MakeDoubleAccessor (&ShaperNetDeviceFace::m_headroom),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("UpdateInterval",
                   "Interval to update observed incoming interest rate.",
                   TimeValue (Seconds(0.1)),
                   MakeTimeAccessor (&ShaperNetDeviceFace::m_updateInterval),
                   MakeTimeChecker ())
    .AddAttribute ("QueueMode", 
                   "Determine when to reject/drop an interest (DropTail/PIE/CoDel)",
                   EnumValue (QUEUE_MODE_DROPTAIL),
                   MakeEnumAccessor (&ShaperNetDeviceFace::SetMode),
                   MakeEnumChecker (QUEUE_MODE_DROPTAIL, "QUEUE_MODE_DROPTAIL",
                                    QUEUE_MODE_PIE, "QUEUE_MODE_PIE",
                                    QUEUE_MODE_CODEL, "QUEUE_MODE_CODEL"))
    .AddAttribute ("DelayTarget",
                   "Target queueing delay (for PIE or CoDel).",
                   TimeValue (Seconds(0.02)),
                   MakeTimeAccessor (&ShaperNetDeviceFace::m_delayTarget),
                   MakeTimeChecker ())
    .AddAttribute ("MaxBurst",
                   "Maximum burst allowed before random early drop kicks in (for PIE).",
                   TimeValue (Seconds(0.1)),
                   MakeTimeAccessor (&ShaperNetDeviceFace::m_maxBurst),
                   MakeTimeChecker ())
    .AddAttribute ("DelayObserveInterval",
                   "Interval to observe minimum packet sojourn time (for CoDel).",
                   TimeValue (Seconds(0.1)),
                   MakeTimeAccessor (&ShaperNetDeviceFace::m_delayObserveInterval),
                   MakeTimeChecker ())
    ;
  return tid;
}

ShaperNetDeviceFace::ShaperNetDeviceFace (Ptr<Node> node, const Ptr<NetDevice> &netDevice)
  : NetDeviceFace (node, netDevice)
  , m_lastUpdateTime (0.0)
  , m_byteSinceLastUpdate (0)
  , m_observedInInterestBitRate (0.0)
  , m_outInterestSize (40)
  , m_inInterestSize (40)
  , m_outContentSize (1100)
  , m_inContentSize (1100)
  , m_outInterestFirst (true)
  , m_inInterestFirst (true)
  , m_outContentFirst (true)
  , m_inContentFirst (true)
  , m_shaperState (OPEN)
  , m_old_delay (0.0)
  , m_drop_prob (0.0)
  , m_dq_count (-1)
  , m_avg_dq_rate (0.0)
  , m_dq_start (0.0)
  , m_burst_allowance (Seconds(0.1))
  , m_first_above_time (0.0)
  , m_drop_next (0.0)
  , m_drop_count (0)
  , m_dropping (false)
{
  DataRateValue dataRate;
  netDevice->GetAttribute ("DataRate", dataRate);
  m_outBitRate = dataRate.Get().GetBitRate();
  m_inBitRate = m_outBitRate; // assume symmetric bandwidth, can be overridden by SetInRate()
}

ShaperNetDeviceFace::~ShaperNetDeviceFace ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

ShaperNetDeviceFace& ShaperNetDeviceFace::operator= (const ShaperNetDeviceFace &)
{
  return *this;
}

void
ShaperNetDeviceFace::SetInRate (DataRateValue inRate)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_inBitRate = inRate.Get().GetBitRate();
}

void
ShaperNetDeviceFace::SetMode (ShaperNetDeviceFace::QueueMode mode)
{
  NS_LOG_FUNCTION (this << mode);
  m_mode = mode;
  if (m_mode == QUEUE_MODE_PIE)
    Simulator::Schedule (Seconds(0.03), &ShaperNetDeviceFace::PIEUpdate, this);
}

ShaperNetDeviceFace::QueueMode
ShaperNetDeviceFace::GetMode (void)
{
  NS_LOG_FUNCTION (this);
  return m_mode;
}

void
ShaperNetDeviceFace::PIEUpdate ()
{
  NS_LOG_FUNCTION (this);

  double qdelay;
  if (m_avg_dq_rate > 0)
    qdelay = m_interestQueue.size() / m_avg_dq_rate;
  else
    qdelay = 0.0;

  NS_LOG_LOGIC(this << " PIE qdelay: " << qdelay << " old delay: " << m_old_delay);

  double tmp_p = 0.125 * (qdelay - m_delayTarget.GetSeconds()) + 1.25 * (qdelay - m_old_delay);
  if (m_drop_prob < 0.01)
      tmp_p /= 8.0;
  else if (m_drop_prob < 0.1)
      tmp_p /= 2.0;

  tmp_p += m_drop_prob;
  if (tmp_p < 0)
    m_drop_prob = 0.0;
  else if (tmp_p > 1)
    m_drop_prob = 1.0;
  else
    m_drop_prob = tmp_p;

  NS_LOG_LOGIC(this << " PIE: udpate drop probability to " << m_drop_prob);

  if (qdelay < m_delayTarget.GetSeconds() / 2 && m_old_delay < m_delayTarget.GetSeconds() / 2 and m_drop_prob == 0.0)
    {
      m_dq_count = -1;
      m_avg_dq_rate = 0.0;
      m_burst_allowance = m_maxBurst;
    }

  m_old_delay = qdelay;
  Simulator::Schedule (Seconds(0.03), &ShaperNetDeviceFace::PIEUpdate, this);
}

bool
ShaperNetDeviceFace::SendImpl (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this << p);

  HeaderHelper::Type type = HeaderHelper::GetNdnHeaderType (p);
  switch (type)
    {
    case HeaderHelper::INTEREST_NDNSIM:
      {
        Ptr<Packet> packet = p->Copy ();
        Ptr<InterestHeader> header = Create<InterestHeader> ();
        packet->RemoveHeader (*header);
        if (header->GetNack () > 0)
          return NetDeviceFace::SendImpl (p); // no shaping for NACK packets

        NS_LOG_LOGIC(this << " shaper qlen: " << m_interestQueue.size());

        if(m_interestQueue.size() < m_maxInterest)
          {
            if (m_mode == QUEUE_MODE_PIE)
              {
                if (m_burst_allowance <= 0 && !(m_old_delay < m_delayTarget.GetSeconds() / 2 && m_drop_prob < 0.2))
                  {
                    NS_LOG_LOGIC(this << " PIE: flip a coin to decide to drop or not " << m_drop_prob);
                    UniformVariable r (0.0, 1.0);
                    if (r.GetValue () < m_drop_prob)
                      {
                        NS_LOG_LOGIC(this << " PIE drop");
                        return false;
                      }
                  }
              }
            else if (m_mode == QUEUE_MODE_CODEL)
              {
                if (m_dropping && Simulator::Now() >= m_drop_next)
                  {
                    NS_LOG_LOGIC(this << " CoDel drop");
                    m_drop_count++;
                    m_drop_next += Seconds(m_delayObserveInterval.GetSeconds() / sqrt(m_drop_count));
                    return false;
                  }

                TimestampTag tag;
                p->AddPacketTag(tag);
              }

            // Enqueue success
            m_interestQueue.push(p);

            if (m_shaperState == OPEN)
              ShaperDequeue();

            return true;
          }
        else
          {
            NS_LOG_LOGIC(this << " Tail drop");
            return false;
          }
      }
    case HeaderHelper::CONTENT_OBJECT_NDNSIM:
      {
        if (m_outContentFirst)
          {
            m_outContentSize = p->GetSize(); // first sample
            m_outContentFirst = false;
          }
        else
          {
            m_outContentSize += (p->GetSize() - m_outContentSize) / 8.0; // smoothing
          }

        return NetDeviceFace::SendImpl (p); // no shaping for content packets
      }
    default:
      return false;
    }
}

void
ShaperNetDeviceFace::ShaperOpen ()
{
  NS_LOG_FUNCTION (this);

  if (m_interestQueue.size() > 0)
    {
      ShaperDequeue();
    }
  else
    {
      m_shaperState = OPEN;

      if (m_mode == QUEUE_MODE_CODEL)
        {
          if (m_dropping)
            {
              // leave dropping state if queue is empty
              NS_LOG_LOGIC(this << " CoDel: leave dropping state due to empty queue");
              m_first_above_time = Seconds(0.0);
              m_dropping = false;
            }
        }
    }
}

void
ShaperNetDeviceFace::ShaperDequeue ()
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC(this << " shaper qlen: " << m_interestQueue.size());

  Ptr<Packet> p = m_interestQueue.front ();
  m_interestQueue.pop ();

  if (m_mode == QUEUE_MODE_PIE)
    {
      if (m_dq_count == -1 && m_interestQueue.size() >= 10)
        {
          // start a measurement cycle
          NS_LOG_LOGIC(this << " PIE: start a measurement cycle");
          m_dq_start = Simulator::Now();
          m_dq_count = 0;
        }

      if (m_dq_count != -1)
        {
          m_dq_count += 1;

          if (m_dq_count >= 10)
            {
              // done with a measurement cycle
              NS_LOG_LOGIC(this << " PIE: done with a measurement cycle");

              Time tmp = Simulator::Now() - m_dq_start;
              if (m_avg_dq_rate == 0.0)
                m_avg_dq_rate = m_dq_count / tmp.GetSeconds();
              else
                m_avg_dq_rate = 0.9 * m_avg_dq_rate + 0.1 * m_dq_count / tmp.GetSeconds();

              NS_LOG_LOGIC(this << " PIE: average dequeue rate " << m_avg_dq_rate);

              if (m_interestQueue.size() >= 10)
                {
                  // start a measurement cycle
                  NS_LOG_LOGIC(this << " PIE: start a measurement cycle");
                  m_dq_start = Simulator::Now();
                  m_dq_count = 0;
                }
              else
                {
                  m_dq_count = -1;
                }

              if (m_burst_allowance > 0)
                m_burst_allowance -= tmp;

              NS_LOG_LOGIC(this << " PIE: burst allowance " << m_burst_allowance);
            }
        }
    }
  else if (m_mode == QUEUE_MODE_CODEL)
    {
      TimestampTag tag;
      p->PeekPacketTag (tag);
      Time sojourn_time = Simulator::Now() - tag.GetTimestamp ();
      NS_LOG_LOGIC(this << " CoDel sojourn time: " << sojourn_time);
      p->RemovePacketTag (tag);

      if (m_dropping && sojourn_time < m_delayTarget)
        {
          // leave dropping state
          NS_LOG_LOGIC(this << " CoDel: leave dropping state due to low delay");
          m_first_above_time = Seconds(0.0);
          m_dropping = false;
        }
      else if (!m_dropping && sojourn_time >= m_delayTarget)
        {
          if (m_first_above_time == Seconds(0.0))
            {
              NS_LOG_LOGIC(this << " CoDel: first above time " << Simulator::Now());
              m_first_above_time = Simulator::Now() + m_delayObserveInterval;
            }
          else if (Simulator::Now() >= m_first_above_time
                   && (Simulator::Now() - m_drop_next < m_delayObserveInterval || Simulator::Now() - m_first_above_time >= m_delayObserveInterval))
            {
              // enter dropping state
              NS_LOG_LOGIC(this << " CoDel: enter dropping state");
              m_dropping = true;

              if (Simulator::Now() - m_drop_next < m_delayObserveInterval)
                m_drop_count = m_drop_count>2 ? m_drop_count-2 : 0;
              else
                m_drop_count = 0;

              m_drop_next = Simulator::Now();
            }
        }
    }

  if (m_outInterestFirst)
    {
      m_outInterestSize = p->GetSize(); // first sample
      m_outInterestFirst = false;
    }
  else
    {
      m_outInterestSize += (p->GetSize() - m_outInterestSize) / 8.0; // smoothing
    }

  m_shaperState = BLOCKED;

  double r1 = m_inContentSize / m_outInterestSize;
  double r2 = m_outContentSize / m_inInterestSize;
  double c1_over_c2 = 1.0 * m_outBitRate / m_inBitRate;

  NS_LOG_LOGIC("r1: " << r1 << ", r2: " << r2);

  // calculate max shaping rate when there's no demand in the reverse direction
  double maxBitRate = m_inBitRate / r1;

  // calculate min shaping rate when there's infinite demand in the reverse direction
  double minBitRate, expectedInInterestBitRate;
  if (c1_over_c2 < (2 * r2) / (r1 * r2 + 1))
    {
      minBitRate = m_outBitRate / 2.0;
      expectedInInterestBitRate = m_outBitRate / (2 * r2);
    }
  else if (c1_over_c2 > (r1 * r2 + 1) / (2 * r1))
    {
      minBitRate = m_inBitRate / (2 * r1);
      expectedInInterestBitRate = m_inBitRate / 2.0;
    }
  else
    {
      minBitRate = (r2 * m_inBitRate - m_outBitRate) / (r1 * r2 - 1);
      expectedInInterestBitRate = (r1 * m_outBitRate - m_inBitRate) / (r1 * r2 - 1);
    }

  expectedInInterestBitRate *= m_headroom;
  NS_LOG_LOGIC("Max shaping rate: " << maxBitRate << "bps, Min shaping rate: " << minBitRate << "bps");

  // determine actual shaping rate based on observedInInterestBitRate and expectedInInterestBitRate
  double shapingBitRate;
  if (m_observedInInterestBitRate >= expectedInInterestBitRate)
    shapingBitRate = minBitRate;
  else
    shapingBitRate = minBitRate + (maxBitRate - minBitRate) * (1.0 - m_observedInInterestBitRate / expectedInInterestBitRate) * (1.0 - m_observedInInterestBitRate / expectedInInterestBitRate);

  NS_LOG_LOGIC("Observed incoming interest rate: " << m_observedInInterestBitRate << "bps, Expected incoming interest rate: " << expectedInInterestBitRate << "bps");

  shapingBitRate *= m_headroom;
  Time gap = Seconds (p->GetSize() * 8.0 / shapingBitRate);

  NS_LOG_LOGIC("Actual shaping rate: " << shapingBitRate << "bps, Gap: " << gap);

  Simulator::Schedule (gap, &ShaperNetDeviceFace::ShaperOpen, this);

  // send out the interest
  NetDeviceFace::SendImpl (p);
}

void
ShaperNetDeviceFace::ReceiveFromNetDevice (Ptr<NetDevice> device,
                                     Ptr<const Packet> p,
                                     uint16_t protocol,
                                     const Address &from,
                                     const Address &to,
                                     NetDevice::PacketType packetType)
{
  NS_LOG_FUNCTION (device << p << protocol << from << to << packetType);

  HeaderHelper::Type type = HeaderHelper::GetNdnHeaderType (p);
  switch (type)
    {
    case HeaderHelper::INTEREST_NDNSIM:
    case HeaderHelper::INTEREST_CCNB:
      {
        if (m_inInterestFirst)
          {
            m_inInterestSize = p->GetSize(); // first sample
            m_inInterestFirst = false;
            m_lastUpdateTime = Simulator::Now();
            m_byteSinceLastUpdate = p->GetSize();
          }
        else
          {
            m_inInterestSize += (p->GetSize() - m_inInterestSize) / 8.0; // smoothing

            if (Simulator::Now() - m_lastUpdateTime >= m_updateInterval)
              {
                m_observedInInterestBitRate = m_byteSinceLastUpdate * 8.0 / (Simulator::Now() - m_lastUpdateTime).GetSeconds();
                m_lastUpdateTime = Simulator::Now();
                m_byteSinceLastUpdate = 0;
              }
            else
              {
                m_byteSinceLastUpdate += p->GetSize();
              }
          }

        break;
      }
    case HeaderHelper::CONTENT_OBJECT_CCNB:
    case HeaderHelper::CONTENT_OBJECT_NDNSIM:
      {
        if (m_inContentFirst)
          {
            m_inContentSize = p->GetSize(); // first sample
            m_inContentFirst = false;
          }
        else
          {
            m_inContentSize += (p->GetSize() - m_inContentSize) / 8.0; // smoothing
          }

        break;
      }
    }

  Receive (p);
}

} // namespace ndnsim
} // namespace ns3

