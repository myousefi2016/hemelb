// 
// Copyright (C) University College London, 2007-2012, all rights reserved.
// 
// This file is part of HemeLB and is CONFIDENTIAL. You may not work 
// with, install, use, duplicate, modify, redistribute or share this
// file, or any part thereof, other than as allowed by any agreement
// specifically made by you with University College London.
// 

#include <algorithm>

#include "geometry/neighbouring/NeighbouringDataManager.h"
#include "geometry/LatticeData.h"

#include "log/Logger.h"
namespace hemelb
{
  namespace geometry
  {
    namespace neighbouring
    {

      NeighbouringDataManager::NeighbouringDataManager(const LatticeData & localLatticeData,
                                                       NeighbouringLatticeData & neighbouringLatticeData,
                                                       net::InterfaceDelegationNet & net) :
          localLatticeData(localLatticeData), neighbouringLatticeData(neighbouringLatticeData), net(net), needsEachProcHasFromMe(net.GetCommunicator().GetSize()), needsHaveBeenShared(false)
      {
      }
      void NeighbouringDataManager::RegisterNeededSite(site_t globalId, RequiredSiteInformation requirements)
      {
        // For now, ignore the requirements, we require everying.
        if (std::find(neededSites.begin(), neededSites.end(), globalId) == neededSites.end())
        {
          neededSites.push_back(globalId);
        }
        else
        {
          // Merge requirements.
        }
      }

      proc_t NeighbouringDataManager::ProcForSite(site_t site)
      {
        return localLatticeData.ProcProvidingSiteByGlobalNoncontiguousId(site);
      }

      void NeighbouringDataManager::TransferNonFieldDependentInformation()
      {
        // Ordering is important here, to ensure the requests are registered in the same order
        // on the sending and receiving procs.
        // But, the needsEachProcHasFromMe is always ordered,
        // by the same order, as the neededSites, so this should be OK.
        for (std::vector<site_t>::iterator localNeed = neededSites.begin(); localNeed != neededSites.end(); localNeed++)
        {
          proc_t source = ProcForSite(*localNeed);
          NeighbouringSite site = neighbouringLatticeData.GetSite(*localNeed);

          net.RequestReceiveR(site.GetSiteData().GetIntersectionData(), source);
          net.RequestReceiveR(site.GetSiteData().GetOtherRawData(), source);
          net.RequestReceive(site.GetWallDistances(), localLatticeData.GetLatticeInfo().GetNumVectors() - 1, source);
          net.RequestReceiveR(site.GetWallNormal(), source);
        }

        for (proc_t other = 0; other < net.GetCommunicator().GetSize(); other++)
        {
          for (std::vector<site_t>::iterator needOnProcFromMe = needsEachProcHasFromMe[other].begin();
              needOnProcFromMe != needsEachProcHasFromMe[other].end(); needOnProcFromMe++)
          {
            site_t localContiguousId =
                localLatticeData.GetLocalContiguousIdFromGlobalNoncontiguousId(*needOnProcFromMe);

            Site site = const_cast<LatticeData&>(localLatticeData).GetSite(localContiguousId);
            // have to cast away the const, because no respect for const-ness for sends in MPI
            net.RequestSendR(site.GetSiteData().GetIntersectionData(), other);
            net.RequestSendR(site.GetSiteData().GetOtherRawData(), other);
            net.RequestSend(site.GetWallDistances(), localLatticeData.GetLatticeInfo().GetNumVectors() - 1, other);
            net.RequestSendR(site.GetWallNormal(), other);
          }
        }
        net.Dispatch();
      }

      void NeighbouringDataManager::TransferFieldDependentInformation()
      {
        RequestComms();
        net.Dispatch();
      }

      void NeighbouringDataManager::RequestComms()
      {
        if (needsHaveBeenShared == false)
        {
          hemelb::log::Logger::Log<hemelb::log::Debug, hemelb::log::OnePerCore>("NDM needs are shared now.");
          ShareNeeds();
        }

        // Ordering is important here, to ensure the requests are registered in the same order
        // on the sending and receiving procs.
        // But, the needsEachProcHasFromMe is always ordered,
        // by the same order, as the neededSites, so this should be OK.

        hemelb::log::Logger::Log<hemelb::log::Debug, hemelb::log::OnePerCore>("I NEED: %i", neededSites.size());

        // For each locally needed site, request it from its home proc.
        for (std::vector<site_t>::iterator localNeed = neededSites.begin(); localNeed != neededSites.end(); localNeed++)
        {
          proc_t source = ProcForSite(*localNeed);
          NeighbouringSite site = neighbouringLatticeData.GetSite(*localNeed);
          net.RequestReceive(site.GetFOld(localLatticeData.GetLatticeInfo().GetNumVectors()),
                             localLatticeData.GetLatticeInfo().GetNumVectors(),
                             source);
        }

        // For every other core...
        for (proc_t other = 0; other < net.GetCommunicator().GetSize(); other++)
        {
          if (needsEachProcHasFromMe[other].size() > 0)
          {
            hemelb::log::Logger::Log<hemelb::log::Debug, hemelb::log::OnePerCore>("OTHER PROC %i NEED: %i",
                                                                                  other,
                                                                                  needsEachProcHasFromMe[other].size());
          }

          // ... send all site details required by that core.
          for (std::vector<site_t>::iterator needOnProcFromMe = needsEachProcHasFromMe[other].begin();
              needOnProcFromMe != needsEachProcHasFromMe[other].end(); needOnProcFromMe++)
          {
            site_t localContiguousId =
                localLatticeData.GetLocalContiguousIdFromGlobalNoncontiguousId(*needOnProcFromMe);
            Site site = const_cast<LatticeData&>(localLatticeData).GetSite(localContiguousId);
            // have to cast away the const, because no respect for const-ness for sends in MPI
            net.RequestSend(site.GetFOld(localLatticeData.GetLatticeInfo().GetNumVectors()),
                            localLatticeData.GetLatticeInfo().GetNumVectors(),
                            other);

          }
        }
      }

      void NeighbouringDataManager::ShareNeeds()
      {
        if (needsHaveBeenShared)
          return;

        // Build a table of which sites are required from each other proc
        std::vector<std::vector<site_t> > needsIHaveFromEachProc(net.GetCommunicator().GetSize());
        std::vector<int> countOfNeedsIHaveFromEachProc(net.GetCommunicator().GetSize(), 0);

        for (std::vector<site_t>::iterator localNeed = neededSites.begin(); localNeed != neededSites.end(); localNeed++)
        {
          hemelb::log::Logger::Log<hemelb::log::Info, hemelb::log::OnePerCore>("Need registered at %i",
                                                                               ProcForSite(*localNeed));
          needsIHaveFromEachProc[ProcForSite(*localNeed)].push_back(*localNeed);
          countOfNeedsIHaveFromEachProc[ProcForSite(*localNeed)]++;
        }

        // Spread around the number of requirements each proc has from each other proc.
        net.RequestAllToAllSend(countOfNeedsIHaveFromEachProc);
        std::vector<int> countOfNeedsOnEachProcFromMe(net.GetCommunicator().GetSize(), 0);
        net.RequestAllToAllReceive(countOfNeedsOnEachProcFromMe);
        net.Dispatch();

        // For each other proc, send and receive the needs list.
        for (proc_t other = 0; other < net.GetCommunicator().GetSize(); other++)
        {
          // now, for every proc, which I need something from,send the ids of those
          net.RequestSendV(needsIHaveFromEachProc[other], other);
          // and, for every proc, which needs something from me, receive those ids
          needsEachProcHasFromMe[other].resize(countOfNeedsOnEachProcFromMe[other]);
          net.RequestReceiveV(needsEachProcHasFromMe[other], other);
          // In principle, this bit could have been implemented as a separate GatherV onto every proc
          // However, in practice, we expect the needs to be basically local
          // so using point-to-point will be more efficient.
          hemelb::log::Logger::Log<hemelb::log::Debug, hemelb::log::OnePerCore>("needsIHaveFromEachProc[other].size(): %d",
                                                                                needsIHaveFromEachProc[other].size());
          hemelb::log::Logger::Log<hemelb::log::Debug, hemelb::log::OnePerCore>("needsEachProcHasFromMe[other].size(): %d",
                                                                                needsEachProcHasFromMe[other].size());
        }
        net.Dispatch();
        needsHaveBeenShared = true;
        hemelb::log::Logger::Log<hemelb::log::Debug, hemelb::log::OnePerCore>("NDM needs have been shared...");
      }
    }
  }
}
