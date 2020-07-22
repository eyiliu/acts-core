// This file is part of the Acts project.
//
// Copyright (C) 2019 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "ACTFW/Alignment/AlignmentAlgorithm.hpp"

#include <stdexcept>

#include "ACTFW/EventData/ProtoTrack.hpp"
#include "ACTFW/EventData/Track.hpp"
#include "ACTFW/Framework/WhiteBoard.hpp"
#include "Acts/Surfaces/PerigeeSurface.hpp"

FW::AlignmentAlgorithm::AlignmentAlgorithm(Config cfg,
                                           Acts::Logging::Level level)
    : FW::BareAlgorithm("AlignmentAlgorithm", level), m_cfg(std::move(cfg)) {
  if (m_cfg.inputSourceLinks.empty()) {
    throw std::invalid_argument("Missing input source links collection");
  }
  if (m_cfg.inputProtoTracks.empty()) {
    throw std::invalid_argument("Missing input proto tracks collection");
  }
  if (m_cfg.inputInitialTrackParameters.empty()) {
    throw std::invalid_argument(
        "Missing input initial track parameters collection");
  }
  if (m_cfg.outputTrajectories.empty()) {
    throw std::invalid_argument("Missing output trajectories collection");
  }
}

FW::ProcessCode FW::AlignmentAlgorithm::execute(
    const FW::AlgorithmContext& ctx) const {
  // Read input data
  const auto sourceLinks =
      ctx.eventStore.get<SimSourceLinkContainer>(m_cfg.inputSourceLinks);
  const auto protoTracks =
      ctx.eventStore.get<ProtoTrackContainer>(m_cfg.inputProtoTracks);
  const auto initialParameters = ctx.eventStore.get<TrackParametersContainer>(
      m_cfg.inputInitialTrackParameters);

  // Consistency cross checks
  if (protoTracks.size() != initialParameters.size()) {
    ACTS_FATAL("Inconsistent number of proto tracks and parameters");
    return ProcessCode::ABORT;
  }

  // Prepare the input track collection
  std::vector<std::vector<SimSourceLink>> sourceLinkTrackContainer;
  sourceLinkTrackContainer.reserve(protoTracks.size());
  std::vector<SimSourceLink> trackSourceLinks;
  for (std::size_t itrack = 0; itrack < protoTracks.size(); ++itrack) {
    // The list of hits and the initial start parameters
    const auto& protoTrack = protoTracks[itrack];

    // Clear & reserve the right size
    trackSourceLinks.clear();
    trackSourceLinks.reserve(protoTrack.size());

    // Fill the source links via their indices from the container
    for (auto hitIndex : protoTrack) {
      auto sourceLink = sourceLinks.nth(hitIndex);
      if (sourceLink == sourceLinks.end()) {
        ACTS_FATAL("Proto track " << itrack << " contains invalid hit index"
                                  << hitIndex);
        return ProcessCode::ABORT;
      }
      trackSourceLinks.push_back(*sourceLink);
    }
    sourceLinkTrackContainer.push_back(trackSourceLinks);
  }

  // Prepare the output data with MultiTrajectory
  TrajectoryContainer trajectories;
  trajectories.reserve(protoTracks.size());

  // Construct a perigee surface as the target surface for the fitter
  auto pSurface = Acts::Surface::makeShared<Acts::PerigeeSurface>(
      Acts::Vector3D{0., 0., 0.});

  // Set the KalmanFitter options
  Acts::KalmanFitterOptions<Acts::VoidOutlierFinder> kfOptions(
      ctx.geoContext, ctx.magFieldContext, ctx.calibContext,
      Acts::VoidOutlierFinder(), &(*pSurface));

  // Set the alignment options
  AlignmentOptions<Acts::KalmanFitterOptions<Acts::VoidOutlierFinder>>
      alignOptions(kfOptions, m_cfg.alignedTransformUpdater,
                   m_cfg.alignedDetElements, m_cfg.chi2ONdfCutOff,
                   m_cfg.deltaChi2ONdfCutOff, m_cfg.maxNumIterations);

  ACTS_DEBUG("Invoke alignment");
  auto result =
      m_cfg.align(sourceLinkTrackContainer, initialParameters, alignOptions);
  if (result.ok()) {
    ACTS_VERBOSE(
        "Alignment finished with deltaChi2 = " << result.value().deltaChi2);
  } else {
    ACTS_WARNING("Alignment failed with " << result.error());
  }

  // ctx.eventStore.add(m_cfg.outputTrajectories, std::move(trajectories));
  return FW::ProcessCode::SUCCESS;
}
