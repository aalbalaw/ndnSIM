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
    .AddAttribute ("MaxContent",
                 "Size of the content queue.",
                 UintegerValue (100),
                 MakeUintegerAccessor (&ShaperNetDeviceFace::m_maxContent),
                 MakeUintegerChecker<uint32_t> ())
    ;
  return tid;
}

ShaperNetDeviceFace::ShaperNetDeviceFace (Ptr<Node> node, const Ptr<NetDevice> &netDevice)
  : NetDeviceFace (node, netDevice)
  , m_outInterestSize (0)
  , m_inInterestSize (0)
  , m_outContentSize (0)
  , m_inContentSize (0)
{
}

ShaperNetDeviceFace::~ShaperNetDeviceFace ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

ShaperNetDeviceFace& ShaperNetDeviceFace::operator= (const ShaperNetDeviceFace &)
{
  return *this;
}

bool
ShaperNetDeviceFace::SendImpl (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  return NetDeviceFace::SendImpl (packet);
}

// callback
void
ShaperNetDeviceFace::ReceiveFromNetDevice (Ptr<NetDevice> device,
                                     Ptr<const Packet> p,
                                     uint16_t protocol,
                                     const Address &from,
                                     const Address &to,
                                     NetDevice::PacketType packetType)
{
  NS_LOG_FUNCTION (device << p << protocol << from << to << packetType);
  Receive (p);
}

} // namespace ndnsim
} // namespace ns3

