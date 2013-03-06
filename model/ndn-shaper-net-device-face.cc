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

#include "ns3/point-to-point-net-device.h"
#include "ns3/channel.h"
#include "ns3/ndn-name-components.h"
#include "ns3/ndn-header-helper.h"
#include "ns3/simulator.h"

NS_LOG_COMPONENT_DEFINE ("ndn.ShaperNetDeviceFace");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED (ShaperNetDeviceFace);

TypeId
ShaperNetDeviceFace::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::ndn::ShaperNetDeviceFace")
    .SetParent<NetDeviceFace> ()
    .SetGroupName ("Ndn")
    .AddAttribute ("MaxInterest",
                 "Size of the interest queue.",
                 UintegerValue (100),
                 MakeUintegerAccessor (&ShaperNetDeviceFace::m_maxInterest),
                 MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Headroom",
                 "Headroom in interest shaping to absorb burstiness.",
                 DoubleValue (0.98),
                 MakeDoubleAccessor (&ShaperNetDeviceFace::m_headroom),
                 MakeDoubleChecker<double> ())
    ;
  return tid;
}

ShaperNetDeviceFace::ShaperNetDeviceFace (Ptr<Node> node, const Ptr<NetDevice> &netDevice)
  : NetDeviceFace (node, netDevice)
  , m_outInterestSize (40)
  , m_inInterestSize (40)
  , m_outContentSize (1100)
  , m_inContentSize (1100)
  , m_outInterestFirst (true)
  , m_inInterestFirst (true)
  , m_outContentFirst (true)
  , m_inContentFirst (true)
  , m_shaperState (OPEN)
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
ShaperNetDeviceFace::SetInRate (DataRate inRate)
{
  NS_LOG_FUNCTION_NOARGS ();
  m_inBitRate = inRate.GetBitRate();
}

bool
ShaperNetDeviceFace::SendImpl (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this << p);

  HeaderHelper::Type type = HeaderHelper::GetNdnHeaderType (p);
  switch (type)
    {
    case HeaderHelper::INTEREST_NDNSIM:
    case HeaderHelper::INTEREST_CCNB:
      {
        if(m_interestQueue.size() < m_maxInterest)
          {
            m_interestQueue.push(p);

            if (m_shaperState == OPEN)
              ShaperDequeue();

            return true;
          }
        else
          {
            // TODO: trace dropped interests
            return false;
          }
      }
    case HeaderHelper::CONTENT_OBJECT_CCNB:
    case HeaderHelper::CONTENT_OBJECT_NDNSIM:
      {
        if (m_outContentFirst)
          {
            m_outContentSize = p->GetSize(); // first sample
            m_outContentFirst = false;
          }
        else
          {
            m_outContentSize += (p->GetSize() - m_outContentSize) >> 3; // smoothing
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
  NS_LOG_FUNCTION_NOARGS ();

  if (m_interestQueue.size() > 0)
    ShaperDequeue();
  else
    m_shaperState = OPEN;
}

void
ShaperNetDeviceFace::ShaperDequeue ()
{
  NS_LOG_FUNCTION_NOARGS ();

  Ptr<Packet> p = m_interestQueue.front ();
  m_interestQueue.pop ();

  if (m_outInterestFirst)
    {
      m_outInterestSize = p->GetSize(); // first sample
      m_outInterestFirst = false;
    }
  else
    {
      m_outInterestSize += (p->GetSize() - m_outInterestSize) >> 3; // smoothing
    }

  m_shaperState = BLOCKED;

  // calculate max shaping rate when there's no demand in the reverse direction
  double maxBitRate = 1.0 * m_inBitRate * m_outInterestSize / m_inContentSize;

  // calculate min shaping rate when there's infinite demand in the reverse direction
  double r1 = 1.0 * m_inContentSize / m_outInterestSize;
  double r2 = 1.0 * m_outContentSize / m_inInterestSize;
  double c1_over_c2 = 1.0 * m_outBitRate / m_inBitRate;

  NS_LOG_LOGIC("r1: " << r1 << ", r2: " << r2);

  double minBitRate;
  if (c1_over_c2 < (2 * r2) / (r1 * r2 + 1))
    minBitRate = m_outBitRate / 2.0;
  else if (c1_over_c2 > (r1 * r2 + 1) / (2 * r1))
    minBitRate = m_inBitRate / (2 * r1);
  else
    minBitRate = (r2 * m_inBitRate - m_outBitRate) / (r1 * r2 - 1);

  // determine actual shaping rate
  NS_LOG_LOGIC("Max shaping rate: " << maxBitRate << "bps, Min shaping rate: " << minBitRate << "bps");

  double shapingBitRate = std::max<double>(maxBitRate, minBitRate);
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
          }
        else
          {
            m_inInterestSize += (p->GetSize() - m_inInterestSize) >> 3; // smoothing
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
            m_inContentSize += (p->GetSize() - m_inContentSize) >> 3; // smoothing
          }

        break;
      }
    }

  Receive (p);
}

} // namespace ndnsim
} // namespace ns3

