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
 * Author: Yaogong Wang <ywang15@ncsu.edu>
 */
// ndn-cc-3: asymmetric bandwidth
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/point-to-point-module.h"
#include <ns3/ndnSIM/utils/tracers/ndn-l3-aggregate-tracer.h>
#include <ns3/ndnSIM/utils/tracers/ndn-app-delay-tracer.h>

using namespace ns3;

int
main (int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse (argc, argv);

  // Setup topology manually
  NodeContainer nodes;
  nodes.Create (4);

  Config::SetDefault ("ns3::PointToPointNetDevice::DataRate", StringValue ("100Mbps"));
  Config::SetDefault ("ns3::PointToPointChannel::Delay", StringValue ("10ms"));
  Config::SetDefault ("ns3::DropTailQueue::MaxPackets", StringValue ("60"));

  PointToPointHelper p2p;
  p2p.Install (nodes.Get (0), nodes.Get (1));
  p2p.Install (nodes.Get (2), nodes.Get (3));

  ObjectFactory factory;
  factory.SetTypeId("ns3::PointToPointNetDevice");
  Config::SetDefault ("ns3::PointToPointNetDevice::DataRate", StringValue ("1Mbps"));
  Ptr<PointToPointNetDevice> devA = factory.Create<PointToPointNetDevice> ();
  devA->SetAddress (Mac48Address::Allocate ());
  nodes.Get (1)->AddDevice (devA);

  factory.SetTypeId("ns3::DropTailQueue");
  Ptr<Queue> queueA = factory.Create<Queue> ();
  devA->SetQueue (queueA);

  factory.SetTypeId("ns3::PointToPointNetDevice");
  Config::SetDefault ("ns3::PointToPointNetDevice::DataRate", StringValue ("10Mbps"));
  Ptr<PointToPointNetDevice> devB = factory.Create<PointToPointNetDevice> ();
  devB->SetAddress (Mac48Address::Allocate ());
  nodes.Get (2)->AddDevice (devB);

  factory.SetTypeId("ns3::DropTailQueue");
  Ptr<Queue> queueB = factory.Create<Queue> ();
  devB->SetQueue (queueB);

  factory.SetTypeId("ns3::PointToPointChannel");
  Ptr<PointToPointChannel> channel = factory.Create<PointToPointChannel> ();
  devA->Attach (channel);
  devB->Attach (channel);

  // Install CCNx stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.SetForwardingStrategy ("ns3::ndn::fw::BestRoute",
                                   "EnableNACKs", "true");
  ndnHelper.EnableShaper (true);
  ndnHelper.SetContentStore ("ns3::ndn::cs::Lru", "MaxSize", "1"); // almost no caching
  ndnHelper.InstallAll ();

  // Installing global routing interface on all nodes
  ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll ();

  // Getting containers for the consumer/producer
  Ptr<Node> cp1 = nodes.Get (0);
  Ptr<Node> cp2 = nodes.Get (3);

  // Install consumer
  ndn::AppHelper consumerHelper ("ns3::ndn::ConsumerWindowAIMD");

  consumerHelper.SetPrefix ("/cp2");
  consumerHelper.Install (cp1);

  consumerHelper.SetPrefix ("/cp1");
  consumerHelper.Install (cp2);

  // Register prefix with global routing controller and install producer
  ndn::AppHelper producerHelper ("ns3::ndn::Producer");
  producerHelper.SetAttribute ("PayloadSize", StringValue("1000"));

  ndnGlobalRoutingHelper.AddOrigins ("/cp1", cp1);
  producerHelper.SetPrefix ("/cp1");
  producerHelper.Install (cp1);

  ndnGlobalRoutingHelper.AddOrigins ("/cp2", cp2);
  producerHelper.SetPrefix ("/cp2");
  producerHelper.Install (cp2);

  // Calculate and install FIBs
  ndnGlobalRoutingHelper.CalculateRoutes ();

  Simulator::Stop (Seconds (60.1));

  boost::tuple< boost::shared_ptr<std::ostream>, std::list<Ptr<ndn::L3AggregateTracer> > >
    aggTracers = ndn::L3AggregateTracer::InstallAll ("aggregate-trace.txt", Seconds (60.0));

  boost::tuple< boost::shared_ptr<std::ostream>, std::list<Ptr<ndn::AppDelayTracer> > >
   tracers = ndn::AppDelayTracer::InstallAll ("app-delays-trace.txt");

  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
