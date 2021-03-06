/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
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
 */

#include "ndn-consumer-rate-feedback.h"
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/callback.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"

#include "ns3/ndn-app-face.h"
#include "ns3/ndn-interest.h"
#include "ns3/ndn-content-object.h"

#include <boost/lexical_cast.hpp>

NS_LOG_COMPONENT_DEFINE ("ndn.ConsumerRateFeedback");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED (ConsumerRateFeedback);

TypeId
ConsumerRateFeedback::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ndn::ConsumerRateFeedback")
    .SetGroupName ("Ndn")
    .SetParent<ConsumerRate> ()
    .AddConstructor<ConsumerRateFeedback> ()

    .AddAttribute ("ProbeFactor",
                   "Rate probing factor",
                   DoubleValue (10.0),
                   MakeDoubleAccessor (&ConsumerRateFeedback::m_probeFactor),
                   MakeDoubleChecker<double> ())
    ;

  return tid;
}

ConsumerRateFeedback::ConsumerRateFeedback ()
  : ConsumerRate ()
  , m_incomingDataFrequency (0.0)
  , m_prevData (0.0)
  , m_inSlowStart (true)
{
}

ConsumerRateFeedback::~ConsumerRateFeedback ()
{
}

void
ConsumerRateFeedback::AdjustFrequencyOnContentObject (const Ptr<const ContentObject> &contentObject,
                                                      Ptr<Packet> payload)
{
  if (m_prevData != Seconds (0.0))
    {
      double freq = 1.0 / (Simulator::Now() - m_prevData).GetSeconds();
      if (m_incomingDataFrequency == 0.0)
        {
          m_incomingDataFrequency = freq;
        }
      else
        {
          m_incomingDataFrequency = m_incomingDataFrequency * 7.0 / 8.0 + freq / 8.0;
          if (freq < m_incomingDataFrequency)
            m_inSlowStart = false;
        }

      if (m_inSlowStart)
        m_frequency = m_incomingDataFrequency * 2.0;
      else
        m_frequency = m_incomingDataFrequency + m_probeFactor;

      NS_LOG_DEBUG ("Current frequency: " << m_frequency);
    }

  m_prevData = Simulator::Now();
}

} // namespace ndn
} // namespace ns3
