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

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndn-shaper-net-device-face.h"
#include <ns3/ndnSIM/utils/tracers/ndn-l3-aggregate-tracer.h>
#include <ns3/ndnSIM/utils/tracers/ndn-app-delay-tracer.h>

using namespace ns3;

int
main (int argc, char *argv[])
{
  std::string consumer ("WindowRelentless"), shaper ("PIE");
  std::string bw_bottle ("10Mbps"), lat_bottle ("13ms"), bw_leg ("1Gbps"), lat_leg ("1ms"), qsize ("38");
  std::string agg_trace ("aggregate-trace.txt"), delay_trace ("app-delays-trace.txt"); 

  CommandLine cmd;
  cmd.AddValue("consumer", "Consumer type (AIMD/CUBIC/RAAQM/WindowRelentless/RateRelentless/RateFeedback)", consumer);
  cmd.AddValue("shaper", "Shaper mode (None/DropTail/PIE/CoDel)", shaper);
  cmd.AddValue("bw_bottle", "Bottleneck link bandwidth", bw_bottle);
  cmd.AddValue("lat_bottle", "Bottleneck link latency", lat_bottle);
  cmd.AddValue("bw_leg", "Leg link bandwidth", bw_leg);
  cmd.AddValue("lat_leg", "Leg link latency", lat_leg);
  cmd.AddValue("qsize", "L2/Shaper queue size", qsize);
  cmd.AddValue("agg_trace", "Aggregate trace file name", agg_trace);
  cmd.AddValue("delay_trace", "App delay trace file name", delay_trace);
  cmd.Parse (argc, argv);

  ndn::ShaperNetDeviceFace::QueueMode mode_enum;
  if (shaper == "DropTail")
    mode_enum = ndn::ShaperNetDeviceFace::QUEUE_MODE_DROPTAIL;
  else if (shaper == "PIE")
    mode_enum = ndn::ShaperNetDeviceFace::QUEUE_MODE_PIE;
  else if (shaper == "CoDel")
    mode_enum = ndn::ShaperNetDeviceFace::QUEUE_MODE_CODEL;
  else if (shaper != "None")
    return -1;

  uint32_t qsize_int;
  std::istringstream (qsize) >> qsize_int;

  // Setup topology
  NodeContainer nodes;
  nodes.Create (10);

  Config::SetDefault ("ns3::DropTailQueue::MaxPackets", StringValue (qsize));

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue ("1Gbps"));
  p2p.SetChannelAttribute("Delay", StringValue ("1ms"));
  p2p.Install (nodes.Get (0), nodes.Get (4));
  p2p.Install (nodes.Get (1), nodes.Get (4));
  p2p.Install (nodes.Get (2), nodes.Get (4));
  p2p.Install (nodes.Get (3), nodes.Get (4));
  p2p.Install (nodes.Get (5), nodes.Get (6));
  p2p.Install (nodes.Get (5), nodes.Get (7));
  p2p.Install (nodes.Get (5), nodes.Get (8));

  p2p.SetDeviceAttribute("DataRate", StringValue (bw_bottle));
  p2p.SetChannelAttribute("Delay", StringValue (lat_bottle));
  p2p.Install (nodes.Get (4), nodes.Get (5));

  p2p.SetDeviceAttribute("DataRate", StringValue (bw_leg));
  p2p.SetChannelAttribute("Delay", StringValue (lat_leg));
  p2p.Install (nodes.Get (5), nodes.Get (9));

  // Install CCNx stack on all nodes
  ndn::StackHelper ndnHelper;
  if (shaper != "None")
    {
      ndnHelper.SetForwardingStrategy ("ns3::ndn::fw::BestRoute", "EnableNACKs", "true");
      ndnHelper.EnableShaper (true, qsize_int, 0.97, Seconds(0.1), mode_enum);
    }
  else
    {
      ndnHelper.SetForwardingStrategy ("ns3::ndn::fw::BestRoute"); // No hop-by-hop interest shaping, no NACKs.
    }
  ndnHelper.SetContentStore ("ns3::ndn::cs::Nocache");
  ndnHelper.InstallAll ();

  // Installing global routing interface on all nodes
  ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll ();

  // Getting containers for the consumer/producer
  Ptr<Node> c1 = nodes.Get (2);
  Ptr<Node> c2 = nodes.Get (3);
  Ptr<Node> p1 = nodes.Get (8);
  Ptr<Node> p2 = nodes.Get (9);

  // Install consumer
  ndn::AppHelper *consumerHelper;
  if (consumer == "AIMD")
    consumerHelper = new ndn::AppHelper ("ns3::ndn::ConsumerWindowAIMD");
  else if (consumer == "CUBIC")
    consumerHelper = new ndn::AppHelper ("ns3::ndn::ConsumerWindowCUBIC");
  else if (consumer == "RAAQM")
    consumerHelper = new ndn::AppHelper ("ns3::ndn::ConsumerWindowRAAQM");
  else if (consumer == "WindowRelentless")
    consumerHelper = new ndn::AppHelper ("ns3::ndn::ConsumerWindowRelentless");
  else if (consumer == "RateRelentless")
    consumerHelper = new ndn::AppHelper ("ns3::ndn::ConsumerRateRelentless");
  else if (consumer == "RateFeedback")
    consumerHelper = new ndn::AppHelper ("ns3::ndn::ConsumerRateFeedback");
  else
    return -1;

  consumerHelper->SetAttribute ("LifeTime", TimeValue (Seconds (5.0)));

  consumerHelper->SetPrefix ("/p1");
  UniformVariable r (0.0, 5.0);
  consumerHelper->SetAttribute ("StartTime", TimeValue (Seconds (r.GetValue ())));
  consumerHelper->Install (c1);

  consumerHelper->SetPrefix ("/p2");
  consumerHelper->SetAttribute ("StartTime", TimeValue (Seconds (r.GetValue ())));
  consumerHelper->Install (c2);

  delete consumerHelper;
  
  // Register prefix with global routing controller and install producer
  ndn::AppHelper producerHelper ("ns3::ndn::Producer");
  producerHelper.SetAttribute ("PayloadSize", StringValue("1000"));

  ndnGlobalRoutingHelper.AddOrigins ("/p1", p1);
  producerHelper.SetPrefix ("/p1");
  producerHelper.Install (p1);

  ndnGlobalRoutingHelper.AddOrigins ("/p2", p2);
  producerHelper.SetPrefix ("/p2");
  producerHelper.Install (p2);

  // CBR as background traffic
  ndn::AppHelper consumerCbrHelper ("ns3::ndn::ConsumerCbr");
  consumerCbrHelper.SetAttribute ("Randomize", StringValue("exponential"));
  consumerCbrHelper.SetAttribute ("RandComponentLenMax", StringValue("16"));
  producerHelper.SetAttribute ("RandomPayloadSizeMin", StringValue("500"));
  producerHelper.SetAttribute ("RandomPayloadSizeMax", StringValue("1000"));

  consumerCbrHelper.SetPrefix ("/cbr1");
  consumerCbrHelper.SetAttribute ("Frequency", StringValue("3"));
  consumerCbrHelper.Install (nodes.Get (0));
  consumerCbrHelper.SetPrefix ("/cbr2");
  consumerCbrHelper.SetAttribute ("Frequency", StringValue("7"));
  consumerCbrHelper.Install (nodes.Get (1));

  ndnGlobalRoutingHelper.AddOrigins ("/cbr1", nodes.Get (6));
  producerHelper.SetPrefix ("/cbr1");
  producerHelper.Install (nodes.Get (6));
  ndnGlobalRoutingHelper.AddOrigins ("/cbr2", nodes.Get (7));
  producerHelper.SetPrefix ("/cbr2");
  producerHelper.Install (nodes.Get (7));

  consumerCbrHelper.SetPrefix ("/cbr3");
  consumerCbrHelper.SetAttribute ("Frequency", StringValue("100"));
  consumerCbrHelper.Install (nodes.Get (6));
  consumerCbrHelper.SetPrefix ("/cbr4");
  consumerCbrHelper.SetAttribute ("Frequency", StringValue("200"));
  consumerCbrHelper.Install (nodes.Get (7));

  ndnGlobalRoutingHelper.AddOrigins ("/cbr3", nodes.Get (1));
  producerHelper.SetPrefix ("/cbr3");
  producerHelper.Install (nodes.Get (1));
  ndnGlobalRoutingHelper.AddOrigins ("/cbr4", nodes.Get (0));
  producerHelper.SetPrefix ("/cbr4");
  producerHelper.Install (nodes.Get (0));

  // Calculate and install FIBs
  ndnGlobalRoutingHelper.CalculateRoutes ();

  Simulator::Stop (Seconds (70.1));

  boost::tuple< boost::shared_ptr<std::ostream>, std::list<Ptr<ndn::L3AggregateTracer> > >
    aggTracers = ndn::L3AggregateTracer::InstallAll (agg_trace, Seconds (10.0));

  boost::tuple< boost::shared_ptr<std::ostream>, std::list<Ptr<ndn::AppDelayTracer> > >
    delayTracers = ndn::AppDelayTracer::InstallAll (delay_trace);

  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
