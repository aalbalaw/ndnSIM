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

#ifndef NDN_SHAPER_NET_DEVICE_FACE_H
#define NDN_SHAPER_NET_DEVICE_FACE_H

#include <queue>
#include "ndn-net-device-face.h"
#include "ns3/net-device.h"
#include "ns3/data-rate.h"

namespace ns3 {
namespace ndn {

/**
 * \ingroup ndn-face
 * \brief Implementation of layer-2 (Ethernet) Ndn face with interest shaping
 *
 * This class adds interest shaping to NdnNetDeviceFace
 *
 * \see NdnNetDeviceFace
 */
class ShaperNetDeviceFace  : public NetDeviceFace
{
public:
  static TypeId
  GetTypeId ();

  /**
   * \brief Constructor
   *
   * @param node Node associated with the face
   * @param netDevice a smart pointer to NetDevice object to which
   * this face will be associate
   */
  ShaperNetDeviceFace (Ptr<Node> node, const Ptr<NetDevice> &netDevice);
  virtual ~ShaperNetDeviceFace();

  void SetInRate (DataRate inRate);

protected:
  virtual bool
  SendImpl (Ptr<Packet> p);

private:
  ShaperNetDeviceFace (const ShaperNetDeviceFace &); ///< \brief Disabled copy constructor
  ShaperNetDeviceFace& operator= (const ShaperNetDeviceFace &); ///< \brief Disabled copy operator

  void ShaperOpen ();
  void ShaperDequeue ();

  virtual void ReceiveFromNetDevice (Ptr<NetDevice> device,
                             Ptr<const Packet> p,
                             uint16_t protocol,
                             const Address &from,
                             const Address &to,
                             NetDevice::PacketType packetType);

  std::queue<Ptr<Packet> > m_interestQueue;
  uint32_t m_maxInterest;
  double m_headroom;

  uint64_t m_outBitRate;
  uint64_t m_inBitRate;

  Time m_updateInterval;
  Time m_lastUpdateTime;
  uint64_t m_byteSinceLastUpdate;
  double m_observedInInterestBitRate;

  uint32_t m_outInterestSize;
  uint32_t m_inInterestSize;
  uint32_t m_outContentSize;
  uint32_t m_inContentSize;

  bool m_outInterestFirst;
  bool m_inInterestFirst;
  bool m_outContentFirst;
  bool m_inContentFirst;

  enum ShaperState
  {
    OPEN,
    BLOCKED
  };

  ShaperState m_shaperState;
};

} // namespace ndn
} // namespace ns3

#endif //NDN_SHAPER_NET_DEVICE_FACE_H
