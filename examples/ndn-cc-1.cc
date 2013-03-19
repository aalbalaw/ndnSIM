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
// ndn-cc-1: randomized interest size
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"
#include <ns3/ndnSIM/utils/tracers/ndn-l3-aggregate-tracer.h>
#include <ns3/ndnSIM/utils/tracers/ndn-app-delay-tracer.h>

using namespace ns3;

int
main (int argc, char *argv[])
{
  std::string agg_trace ("aggregate-trace.txt"), delay_trace ("app-delays-trace.txt"); 

  CommandLine cmd;
  cmd.AddValue("agg_trace", "aggregate trace file name", agg_trace);
  cmd.AddValue("delay_trace", "app delay trace file name", delay_trace);
  cmd.Parse (argc, argv);

  // Read topology
  AnnotatedTopologyReader topologyReader ("", 25);
  topologyReader.SetFileName ("src/ndnSIM/examples/topologies/topo-4-node.txt");
  topologyReader.Read ();

  // Install CCNx stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.SetForwardingStrategy ("ns3::ndn::fw::BestRoute",
                                   "EnableNACKs", "true");
  ndnHelper.EnableShaper (true, 60, 0.98);
  ndnHelper.SetContentStore ("ns3::ndn::cs::Lru", "MaxSize", "1"); // almost no caching
  ndnHelper.InstallAll ();

  // Installing global routing interface on all nodes
  ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll ();

  // Getting containers for the consumer/producer
  Ptr<Node> cp1 = Names::Find<Node> ("CP1");
  Ptr<Node> cp2 = Names::Find<Node> ("CP2");

  // Install consumer
  ndn::AppHelper consumerHelper ("ns3::ndn::ConsumerWindowAIMD");

  consumerHelper.SetPrefix ("/cp2");
  consumerHelper.SetAttribute (std::string("RandComponentLenMax"), IntegerValue(32));
  consumerHelper.Install (cp1);

  consumerHelper.SetPrefix ("/cp1");
  consumerHelper.SetAttribute (std::string("RandComponentLenMax"), IntegerValue(32));
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
    aggTracers = ndn::L3AggregateTracer::InstallAll (agg_trace, Seconds (60.0));

  boost::tuple< boost::shared_ptr<std::ostream>, std::list<Ptr<ndn::AppDelayTracer> > >
    tracers = ndn::AppDelayTracer::InstallAll (delay_trace);

  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
