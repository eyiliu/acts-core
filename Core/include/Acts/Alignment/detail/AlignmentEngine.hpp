// This file is part of the Acts project.
//
// Copyright (C) 2020 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "Acts/EventData/MultiTrajectory.hpp"
#include "Acts/EventData/TrackParameters.hpp"
#include "Acts/Geometry/GeometryContext.hpp"
#include "Acts/Surfaces/Surface.hpp"
#include "Acts/Utilities/Definitions.hpp"
#include "Acts/Utilities/ParameterDefinitions.hpp"

#include <unordered_map>

namespace Acts {
namespace detail {

///
///@brief struct to store info needed for track-based alignment
///
struct TrackAlignmentState {
  // The measurements covariance
  ActsMatrixX<ParValue_t> measurementCovariance;

  // The track parameters covariance
  ActsMatrixX<BoundParametersScalar> trackParametersCovariance;

  // The projection matrix
  ActsMatrixX<BoundParametersScalar> projectionMatrix;

  // The residual
  ActsVectorX<ParValue_t> residual;

  // The covariance of residual
  ActsMatrixX<BoundParametersScalar> residualCovariance;

  // The chi2
  double chi2 = 0;

  // The derivative of residual w.r.t. alignment parameters
  ActsMatrixX<BoundParametersScalar> alignmentToResidualDerivative;

  // The derivative of chi2 w.r.t. alignment parameters
  ActsVectorX<BoundParametersScalar> alignmentToChi2Derivative;

  // The second derivative of chi2 w.r.t. alignment parameters
  ActsMatrixX<BoundParametersScalar> alignmentToChi2SecondDerivative;

  // The alignable surfaces on the track and their indices in both the global
  // alignable surfaces pool and those relevant with this track
  std::unordered_map<const Surface*, std::pair<size_t, size_t>> alignedSurfaces;

  // The dimension of measurements
  size_t measurementDim = 0;

  // The dimension of track parameters
  size_t trackParametersDim = 0;

  // The contributed alignment degree of freedom
  size_t alignmentDof = 0;
};

///
/// Calculate the first and second derivative of chi2 w.r.t. alignment
/// parameters for a single track
///
/// Suppose there are n measurements on the track, and m (m<=n) of them are on
/// alignable surface, then eAlignmentParametersSize*m alignment parameters
/// will be involved for this particular track, i.e. this track will contribute
/// to at most eAlignmentParametersSize*m*2 elements of the full chi2
/// second derivative matrix
///
/// @tparam source_link_t The source link type of the trajectory
/// @tparam parameters_t The track parameters type
///
/// @param multiTraj The MultiTrajectory containing the trajectory to be
/// investigated
/// @param entryIndex The trajectory entry index
/// @param globalTrackParamsCov The global track parameters covariance for a
/// single track and the starting row/column for smoothed states. This contains
/// all smoothed track states including those non-measurement states. Selection
/// of certain rows/columns for measurement states is needed.
/// @param idxedAlignSurfaces The indexed surfaces to be aligned
///
/// @return The track alignment state containing fundamental alignment
/// ingredients
template <typename source_link_t, typename parameters_t = BoundParameters>
TrackAlignmentState trackAlignmentState(
    const MultiTrajectory<source_link_t>& multiTraj, const size_t& entryIndex,
    const std::pair<ActsMatrixX<BoundParametersScalar>,
                    std::unordered_map<size_t, size_t>>& globalTrackParamsCov,
    const std::unordered_map<const Surface*, size_t>& idxedAlignSurfaces) {
  using CovMatrix_t = typename parameters_t::CovMatrix_t;

  // Construct an alignment state
  TrackAlignmentState alignState;

  // Remember the index within the trajectory and whether it's alignable
  std::vector<std::pair<size_t, bool>> measurementStates;
  measurementStates.reserve(15);
  // Number of smoothed states on the track
  size_t nSmoothedStates = 0;
  // Number of alignable surfaces on the track
  size_t nAlignSurfaces = 0;

  // Visit the track states on the track
  multiTraj.visitBackwards(entryIndex, [&](const auto& ts) {
    // Remember the number of smoothed states
    if (ts.hasSmoothed()) {
      nSmoothedStates++;
    }
    // Only measurement states matter (we can't align non-measurement states,
    // no?)
    if (not ts.typeFlags().test(TrackStateFlag::MeasurementFlag)) {
      return true;
    }
    // Check if the reference surface is to be aligned
    // @Todo: consider the case when some of the Dofs are fixed for one surface
    bool isAlignable = false;
    const auto surface = &ts.referenceSurface();
    auto it = idxedAlignSurfaces.find(surface);
    if (it != idxedAlignSurfaces.end()) {
      // Remember the surface and its index
      alignState.alignedSurfaces[surface].first = it->second;
      nAlignSurfaces++;
    }
    // Rember the index of the state within the trajectory and whether it's
    // alignable
    measurementStates.push_back({ts.index(), isAlignable});
    // Add up measurement dimension
    alignState.measurementDim += ts.calibratedSize();
    return true;
  });

  // Return now if the track contains no alignable surfaces
  if (nAlignSurfaces == 0) {
    return alignState;
  }

  // The alignment degree of freedom
  alignState.alignmentDof = eAlignmentParametersSize * nAlignSurfaces;
  // Dimension of global track parameters (from only measurement states)
  alignState.trackParametersDim =
      eBoundParametersSize * measurementStates.size();

  // Initialize the alignment matrixs with components from the measurement
  // states
  // The measurement covariance
  alignState.measurementCovariance =
      ActsMatrixX << ParValue_t >
      ::Zero(alignState.measurementDim, alignState.measurementDim);
  // The bound parameters to measurement projection matrix
  alignState.projectionMatrix = ActsMatrixX<BoundParametersScalar>::Zero(
      alignState.measurementDim, alignState.trackParametersDim);
  // The derivative of residual w.r.t. alignment parameters
  alignState.alignmentToResidualDerivative =
      ActsMatrixX<BoundParametersScalar>::Zero(alignState.measurementDim,
                                               alignState.alignmentDof);
  // The track parameters covariance
  alignState.trackParametersCovariance =
      ActsMatrixX<BoundParametersScalar>::Zero(alignState.trackParametersDim,
                                               alignState.trackParametersDim);
  // The residual
  alignState.residual =
      ActsVectorX<ParValue_t>::Zero(alignState.measurementDim);

  // The dimension of provided global track parameters covariance should be same
  // as eBoundParametersSize * nSmoothedStates
  assert(globalTrackParamsCov.rows() == globalTrackParamsCov.cols() and
         globalTrackParamsCov.rows() == eBoundParametersSize * nSmoothedStates);
  // Unpack global track parameters covariance and the starting row/column for
  // all smoothed states
  const auto& [sourceTrackParamsCov, stateRowIndices] = globalTrackParamsCov;

  // Loop over the measurement states to fill those alignment matrixs
  // This is done in reverse order
  size_t iMeasurement = alignState.measurementDim;
  size_t iParams = alignState.trackParametersDim;
  size_t iSurface = nAlignSurfaces;
  for (const auto& [rowStateIndex, isAlignable] : measurementStates) {
    const auto& state = multiTraj.getTrackState(rowStateIndex);
    size_t measdim = state.calibratedSize();
    // Update index of current measurement and parameter
    iMeasurement -= measdim;
    iParams -= eBoundParametersSize;
    // (a) Get and fill the measurement covariance matrix
    ActsSymMatrixD<measdim> measCovariance =
        state.calibratedCovariance().template topLeftCorner<measdim, measdim>();
    alignState.measurementCovariance.block<measdim, measdim>(
        iMeasurement, iMeasurement) = measCovariance;

    // (b) Get and fill the bound parameters to measurement projection matrix
    const ActsMatrixD<measdim, eBoundParametersSize> H =
        state.projector()
            .template topLeftCorner<measdim, eBoundParametersSize>();
    alignState.projectionMatrix.block<measdim, eBoundParametersSize>(
        iMeasurement, iParams) = H;

    // (c) Get and fill the residual
    alignState.residual.segment<measdim>(iMeasurement) =
        state.calibrated().template head<meas>() - H * state.filtered();

    // (d) @Todo: Get the derivative of alignment parameters w.r.t. measurement
    // or residual
    if (isAlignable) {
      iSurface -= 1;
      const auto surface = &state.referenceSurface();
      alignState.alignedSurfaces.at(surface).second = iSurface;
    }

    // (e) Extract and fill the track parameters covariance matrix for only
    // measurement states
    // @Todo: add helper function to select rows/columns of a matrix
    for (unsigned int iColState = 0; iColState < measurementStates.size();
         iColState++) {
      size_t colStateIndex = measurementStates.at(iColState).first;
      // Retrieve the block from the source covariance matrix
      CovMatrix_t correlation =
          sourceTrackParamsCov
              .block<eBoundParametersSize, eBoundParametersSize>(
                  stateRowIndices.at(rowStateIndex),
                  stateRowIndices.at(colStateIndex));
      // Fill the block of the target covariance matrix
      size_t iCol = trackParametersDim - (iColState + 1) * eBoundParametersSize;
      alignState.trackParametersCovariance
          .block<eBoundParametersSize, eBoundParametersSize>(iParams, iCol) =
          correlation;
    }
  }

  // Calculate the chi2 and chi2 derivatives based on the alignment matrixs
  alignState.chi2 = alignState.residual.transpose() *
                    alignState.measurementCovariance.inverse() *
                    alignState.residual;
  alignState.alignmentToChi2Derivative =
      ActsVectorX<BoundParametersScalar>::Zero(alignState.alignmentDof);
  alignState.alignmentToChi2SecondDerivative =
      ActsMatrixX<BoundParametersScalar>::Zero(alignState.alignmentDof,
                                               alignState.alignmentDof);
  // The covariance of residual
  alignState.residualCovariance = ActsMatrixX<BoundParametersScalar>::Zero(
      alignState.measurementDim, alignState.measurementDim);
  alignState.residualCovariance = alignState.measurementCovariance -
                                  alignState.projectionMatrix *
                                      alignState.trackParametersCovariance *
                                      alignState.projectionMatrix.transpose();

  alignState.alignmentToChi2Derivative =
      2 * alignState.alignmentToResidualDerivative.transpose() *
      alignState.measurementCovariance.inverse() *
      alignState.residualCovariance *
      alignState.measurementCovariance.inverse() * alignState.residual;
  alignState.alignmentToChi2SecondDerivative =
      2 * alignState.alignmentToResidualDerivative.transpose() *
      alignState.measurementCovariance.inverse() *
      alignState.residualCovariance *
      alignState.measurementCovariance.inverse() *
      alignState.alignmentToResidualDerivative;

  return alignState;
}

}  // namespace detail
}  // namespace Acts
