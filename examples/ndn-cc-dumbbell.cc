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
#include <ns3/ndnSIM/utils/tracers/ndn-l3-aggregate-tracer.h>
#include <ns3/ndnSIM/utils/tracers/ndn-app-delay-tracer.h>

using namespace ns3;

int
main (int argc, char *argv[])
{
  std::string bw_23 ("10Mbps"), lat_23 ("3ms"), bw_35 ("1Gbps"), lat_35 ("1ms"), qsize ("15");
  std::string agg_trace ("aggregate-trace.txt"), delay_trace ("app-delays-trace.txt"); 

  CommandLine cmd;
  cmd.AddValue("bw_23", "link bandwidth between 2 and 3", bw_23);
  cmd.AddValue("lat_23", "link latency between 2 and 3", lat_23);
  cmd.AddValue("bw_35", "link bandwidth between 3 and 5", bw_35);
  cmd.AddValue("lat_35", "link latency between 3 and 5", lat_35);
  cmd.AddValue("qsize", "L2/Shaper queue size", qsize);
  cmd.AddValue("agg_trace", "aggregate trace file name", agg_trace);
  cmd.AddValue("delay_trace", "app delay trace file name", delay_trace);
  cmd.Parse (argc, argv);

  uint32_t qsize_int;
  std::istringstream (qsize) >> qsize_int;

  // Setup topology
  NodeContainer nodes;
  nodes.Create (6);

  Config::SetDefault ("ns3::DropTailQueue::MaxPackets", StringValue (qsize));

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue ("1Gbps"));
  p2p.SetChannelAttribute("Delay", StringValue ("1ms"));
  p2p.Install (nodes.Get (0), nodes.Get (2));
  p2p.Install (nodes.Get (1), nodes.Get (2));
  p2p.Install (nodes.Get (3), nodes.Get (4));

  p2p.SetDeviceAttribute("DataRate", StringValue (bw_23));
  p2p.SetChannelAttribute("Delay", StringValue (lat_23));
  p2p.Install (nodes.Get (2), nodes.Get (3));

  p2p.SetDeviceAttribute("DataRate", StringValue (bw_35));
  p2p.SetChannelAttribute("Delay", StringValue (lat_35));
  p2p.Install (nodes.Get (3), nodes.Get (5));

  // Install CCNx stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.SetForwardingStrategy ("ns3::ndn::fw::BestRoute",
                                   "EnableNACKs", "true");
  ndnHelper.EnableShaper (true, qsize_int);
  ndnHelper.SetContentStore ("ns3::ndn::cs::Lru", "MaxSize", "1"); // almost no caching
  ndnHelper.InstallAll ();

  // Installing global routing interface on all nodes
  ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll ();

  // Getting containers for the consumer/producer
  Ptr<Node> c1 = nodes.Get (0);
  Ptr<Node> c2 = nodes.Get (1);
  Ptr<Node> p1 = nodes.Get (4);
  Ptr<Node> p2 = nodes.Get (5);

  // Install consumer
  ndn::AppHelper consumerHelper ("ns3::ndn::ConsumerWindowCUBIC");

  consumerHelper.SetPrefix ("/p1");
  UniformVariable r (0.0, 5.0);
  consumerHelper.SetAttribute ("StartTime", TimeValue (Seconds (r.GetValue ())));
  consumerHelper.Install (c1);

  consumerHelper.SetPrefix ("/p2");
  consumerHelper.SetAttribute ("StartTime", TimeValue (Seconds (r.GetValue ())));
  consumerHelper.Install (c2);
  
  // Register prefix with global routing controller and install producer
  ndn::AppHelper producerHelper ("ns3::ndn::Producer");
  producerHelper.SetAttribute ("PayloadSize", StringValue("1000"));

  ndnGlobalRoutingHelper.AddOrigins ("/p1", p1);
  producerHelper.SetPrefix ("/p1");
  producerHelper.Install (p1);

  ndnGlobalRoutingHelper.AddOrigins ("/p2", p2);
  producerHelper.SetPrefix ("/p2");
  producerHelper.Install (p2);

  // Calculate and install FIBs
  ndnGlobalRoutingHelper.CalculateRoutes ();

  Simulator::Stop (Seconds (70.1));

  boost::tuple< boost::shared_ptr<std::ostream>, std::list<Ptr<ndn::L3AggregateTracer> > >
    aggTracers = ndn::L3AggregateTracer::InstallAll (agg_trace, Seconds (10.0));

  boost::tuple< boost::shared_ptr<std::ostream>, std::list<Ptr<ndn::AppDelayTracer> > >
    tracers = ndn::AppDelayTracer::InstallAll (delay_trace);

  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
