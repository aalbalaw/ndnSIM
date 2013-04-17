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

  /**
   * \brief Destructor
   *
   */
  virtual ~ShaperNetDeviceFace();

  /**
   * \brief Set the data rate in the reverse direction (incoming)
   *
   * @param inRate Data rate in the reverse direction (incoming)
   */
  void SetInRate (DataRateValue inRate);

  /**
   * \brief Enumeration of the queue modes supported by the shaper.
   *
   */
  enum QueueMode
  {
    QUEUE_MODE_DROPTAIL,
    QUEUE_MODE_PIE,
    QUEUE_MODE_CODEL,
  };

  /**
   * Set the queue mode of this shaper.
   *
   * \param mode The queue mode of this shaper.
   *
   */
  void SetMode (QueueMode mode);

  /**
   * Get the queue mode of this shaper.
   *
   * \returns The queue mode of this shaper.
   */
  QueueMode GetMode (void);

protected:
  void PIEUpdate ();

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

  double m_outInterestSize;
  double m_inInterestSize;
  double m_outContentSize;
  double m_inContentSize;

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

  QueueMode m_mode;
  Time m_delayTarget; // for PIE or CoDel
  Time m_maxBurst; // for PIE
  Time m_delayObserveInterval; // for CoDel

  // for PIE
  double m_old_delay;
  double m_drop_prob;
  int64_t m_dq_count;
  double m_avg_dq_rate;
  Time m_dq_start;
  Time m_burst_allowance;

  // for CoDel
  Time m_first_above_time;
  Time m_drop_next;
  uint32_t m_drop_count;
  bool m_dropping;
};

} // namespace ndn
} // namespace ns3

#endif //NDN_SHAPER_NET_DEVICE_FACE_H
