/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011 University of California, Los Angeles
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
 * Author:  Yaogong Wang <ywang15@ncsu.edu>
 *
 */

#include "congestion-aware.h"

#include "ns3/ndn-pit.h"
#include "ns3/ndn-pit-entry.h"
#include "ns3/ndn-interest.h"
#include "ns3/ndn-content-object.h"
#include "ns3/ndn-pit.h"
#include "ns3/ndn-fib.h"
#include "ns3/ndn-fib-entry.h"
#include "ns3/ndn-content-store.h"
#include "ns3/random-variable.h"
#include "ns3/ndnSIM/utils/ndn-fw-hop-count-tag.h"

#include "ns3/assert.h"
#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/boolean.h"
#include "ns3/string.h"

#include <boost/ref.hpp>
#include <boost/foreach.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/lambda/bind.hpp>
#include <boost/tuple/tuple.hpp>
namespace ll = boost::lambda;

NS_LOG_COMPONENT_DEFINE ("ndn.fw.CongestionAware");

namespace ns3 {
namespace ndn {
namespace fw {

NS_OBJECT_ENSURE_REGISTERED (CongestionAware);

TypeId
CongestionAware::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ndn::fw::CongestionAware")
    .SetGroupName ("Ndn")
    .SetParent<Nacks> ()
    .AddConstructor <CongestionAware> ()
    ;
  return tid;
}

CongestionAware::CongestionAware ()
{
}

bool
CongestionAware::DoPropagateInterest (Ptr<Face> inFace,
                                     Ptr<const Interest> header,
                                     Ptr<const Packet> origPacket,
                                     Ptr<pit::Entry> pitEntry)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT_MSG (m_pit != 0, "PIT should be aggregated with forwarding strategy");

  int propagatedCount = 0;

  BOOST_FOREACH (const fib::FaceMetric &metricFace, pitEntry->GetFibEntry ()->m_faces.get<fib::i_nth> ())
    {
      NS_LOG_DEBUG (metricFace.GetFace () << " cnglevel: " << metricFace.GetCngLevel ());
      if (!TrySendOutInterest (inFace, metricFace.GetFace (), header, origPacket, pitEntry))
        {
          if (CanSendOutInterest (inFace, metricFace.GetFace (), header, origPacket, pitEntry))
            pitEntry->GetFibEntry ()->UpdateFaceCngLevelCounter (metricFace.GetFace (), true);

          continue;
        }

      propagatedCount++;
      break; // do only once
    }

  NS_LOG_INFO ("Propagated to " << propagatedCount << " faces");
  return propagatedCount > 0;
}

void
CongestionAware::WillSatisfyPendingInterest (Ptr<Face> inFace,
                                            Ptr<pit::Entry> pitEntry)
{
  if (inFace != 0)
    {
      pitEntry->GetFibEntry ()->UpdateFaceCngLevelCounter (inFace, false);
    }

  super::WillSatisfyPendingInterest (inFace, pitEntry);
}

void
CongestionAware::DidReceiveValidNack (Ptr<Face> inFace,
                                     uint32_t nackCode,
                                     Ptr<const Interest> header,
                                     Ptr<const Packet> origPacket,
                                     Ptr<pit::Entry> pitEntry)
{
  if (inFace != 0 &&
      (nackCode == Interest::NACK_CONGESTION ||
       nackCode == Interest::NACK_GIVEUP_PIT))
    {
      pitEntry->GetFibEntry ()->UpdateFaceCngLevelCounter (inFace, true);
    }

  super::DidReceiveValidNack (inFace, nackCode, header, origPacket, pitEntry);
}


} // namespace fw
} // namespace ndn
} // namespace ns3
