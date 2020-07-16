// This file is part of the Acts project.
//
// Copyright (C) 2020 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <limits>
#include <map>
#include <vector>

#include "ACTFW/Alignment/AlignmentError.hpp"

#include "Acts/Alignment/detail/AlignmentEngine.hpp"
#include "Acts/Fitter/KalmanFitter.hpp"
#include "Acts/Fitter/detail/KalmanGlobalCovariance.hpp"
#include "Acts/Geometry/GeometryContext.hpp"
#include "Acts/MagneticField/MagneticFieldContext.hpp"
#include "Acts/Surfaces/Surface.hpp"
#include "Acts/Utilities/AlignmentDefinitions.hpp"
#include "Acts/Utilities/CalibrationContext.hpp"
#include "Acts/Utilities/Definitions.hpp"
#include "Acts/Utilities/Logger.hpp"
#include "Acts/Utilities/Result.hpp"

namespace FW {
using AlignedTransformUpdater =
    std::function<bool(Acts::DetectorElementBase*, const Acts::GeometryContext&,
                       const Acts::Transform3D&)>;
///
/// @brief Options for align() call
///
/// @tparam fit_options_t The fit options type
///
template <typename fit_options_t>
struct AlignmentOptions {
  /// Deleted default constructor
  AlignmentOptions() = delete;

  /// AlignmentOptions
  ///
  /// @param fOptions The fit options
  /// @param aDetElements The alignable detector elements
  /// @param chi2CufOff The alignment chi2 tolerance
  /// @param maxIters The alignment maximum iterations
  AlignmentOptions(
      const fit_options_t& fOptions,
      const AlignedTransformUpdater& aTransformUpdater,
      const std::vector<Acts::DetectorElementBase*>& aDetElements = {},
      double chi2CutOff = 1, size_t maxIters = 5)
      : fitOptions(fOptions),
        alignedTransformUpdater(aTransformUpdater),
        alignedDetElements(aDetElements),
        deltaChi2CutOff(chi2CutOff),
        maxIterations(maxIters) {}

  // The fit options
  fit_options_t fitOptions;

  /// The updater to the aligned transform
  AlignedTransformUpdater alignedTransformUpdater;

  // The detector elements to be aligned
  std::vector<Acts::DetectorElementBase*> alignedDetElements;

  // The alignment tolerance
  double deltaChi2CutOff = 1;

  // The maximum number of iterations to run alignment
  size_t maxIterations = 5;
};

/// @brief Alignment result struct
///
struct AlignmentResult {
  // The change of alignment parameters
  Acts::ActsVectorX<Acts::BoundParametersScalar> deltaAlignmentParameters;

  // The change of chi2
  double deltaChi2 = 0;

  // The covariance of alignment parameters
  Acts::ActsMatrixX<Acts::BoundParametersScalar> alignmentCovariance;

  // The minimized average chi2 per track
  // double averagedChi2 = std::numeric_limits<double>::max();

  // The number of alignment dof
  size_t dof = 0;

  Acts::Result<void> result{Acts::Result<void>::success()};
};

/// @brief KalmanFitter-based alignment implementation
///
/// @tparam fitter_t Type of the fitter class
template <typename fitter_t>
struct Alignment {
  /// Default constructor is deleted
  Alignment() = delete;

  /// Constructor from arguments
  Alignment(fitter_t fitter,
            std::unique_ptr<const Acts::Logger> logger =
                Acts::getDefaultLogger("Alignment", Acts::Logging::INFO))
      : m_fitter(std::move(fitter)), m_logger(logger.release()) {}

  /// @brief evaluate alignment state for a single track
  ///
  /// @tparam source_link_t Source link type identifying uncalibrated input
  /// measurements.
  /// @tparam start_parameters_t Type of the initial parameters
  /// @tparam fit_options_t The fit options type
  ///
  /// @param gctx The current geometry context object
  /// @param sourcelinks The fittable uncalibrated measurements
  /// @param sParameters The initial track parameters
  /// @param fitOptions The fit Options steering the fit
  /// @param idxedAlignSurfaces The idxed surfaces to be aligned
  ///
  /// @result The alignment state for a single track
  template <typename source_link_t, typename start_parameters_t,
            typename fit_options_t>
  Acts::Result<Acts::detail::TrackAlignmentState> evaluateTrackAlignmentState(
      const Acts::GeometryContext& gctx,
      const std::vector<source_link_t>& sourcelinks,
      const start_parameters_t& sParameters, const fit_options_t& fitOptions,
      const std::unordered_map<const Acts::Surface*, size_t>&
          idxedAlignSurfaces) const {
    // Perform the fit
    auto fitRes = m_fitter.fit(sourcelinks, sParameters, fitOptions);
    if (not fitRes.ok()) {
      return fitRes.error();
    }
    // The fit results
    const auto& fitOutput = fitRes.value();
    // Calculate the global track parameters covariance with the fitted track
    const auto& globalTrackParamsCov =
        Acts::detail::globalTrackParametersCovariance(fitOutput.fittedStates,
                                                      fitOutput.trackTip);
    // Calculate the alignment state
    const auto alignState = Acts::detail::trackAlignmentState(
        gctx, fitOutput.fittedStates, fitOutput.trackTip, globalTrackParamsCov,
        idxedAlignSurfaces);
    if (alignState.alignmentDof == 0) {
      return AlignmentError::NoAlignmentDofOnTrack;
    }
    return alignState;
  }

  /// @brief update the alignment parameters
  ///
  /// @tparam trajectory_container_t The trajectories container type
  /// @tparam start_parameters_t The initial parameters container type
  /// @tparam fit_options_t The fit options type
  ///
  /// @param trajectoryCollection The collection of trajectories as input of
  /// fitting
  /// @param startParametersCollection The collection of starting parameters as
  /// input of fitting
  /// @param fitOptions The fit Options steering the fit
  /// @param alignedDetElements The detector elements to be aligned
  /// @param alignedTransformUpdater The updater for updating the aligned
  /// transform of the detector element
  /// @param alignResult [in, out] The aligned result
  template <typename trajectory_container_t,
            typename start_parameters_container_t, typename fit_options_t>
  Acts::Result<void> updateAlignmentParameters(
      const trajectory_container_t& trajectoryCollection,
      const start_parameters_container_t& startParametersCollection,
      const fit_options_t& fitOptions,
      const std::vector<Acts::DetectorElementBase*>& alignedDetElements,
      const AlignedTransformUpdater& alignedTransformUpdater,
      AlignmentResult& alignResult) const {
    // The number of trajectories must be eual to the number of starting
    // parameters
    assert(trajectoryCollection.size() == startParametersCollection.size());

    // Assign index to the alignable surface
    std::unordered_map<const Acts::Surface*, size_t> idxedAlignSurfaces;
    for (unsigned int iDetElement = 0; iDetElement < alignedDetElements.size();
         iDetElement++) {
      idxedAlignSurfaces.emplace(&alignedDetElements.at(iDetElement)->surface(),
                                 iDetElement);
    }

    // The total alignment degree of freedom
    alignResult.dof =
        idxedAlignSurfaces.size() * Acts::eAlignmentParametersSize;
    // Initialize derivative of chi2 w.r.t. aligment parameters for all tracks
    Acts::ActsVectorX<Acts::BoundParametersScalar> sumChi2Derivative =
        Acts::ActsVectorX<Acts::BoundParametersScalar>::Zero(alignResult.dof);
    Acts::ActsMatrixX<Acts::BoundParametersScalar> sumChi2SecondDerivative =
        Acts::ActsMatrixX<Acts::BoundParametersScalar>::Zero(alignResult.dof,
                                                             alignResult.dof);
    // Copy the fit options
    fit_options_t fitOptionsWithRefSurface = fitOptions;
    // Calculate contribution to chi2 derivatives from all input trajectories
    for (unsigned int iTraj = 0; iTraj < trajectoryCollection.size(); iTraj++) {
      const auto& sourcelinks = trajectoryCollection.at(iTraj);
      const auto& sParameters = startParametersCollection.at(iTraj);
      // Set the target surface
      fitOptionsWithRefSurface.referenceSurface =
          &sParameters.referenceSurface();
      // The result for one single track
      auto evaluateRes = evaluateTrackAlignmentState(
          fitOptions.geoContext, sourcelinks, sParameters,
          fitOptionsWithRefSurface, idxedAlignSurfaces);
      if (not evaluateRes.ok()) {
        ACTS_WARNING("Evaluation of alignment state for track" << iTraj
                                                               << "failed");
        continue;
      }
      const auto& alignState = evaluateRes.value();
      for (const auto& [rowSurface, rows] : alignState.alignedSurfaces) {
        const auto& [dstRow, srcRow] = rows;
        // Fill the results into full chi2 derivative matrixs
        sumChi2Derivative.segment<Acts::eAlignmentParametersSize>(
            dstRow * Acts::eAlignmentParametersSize) +=
            alignState.alignmentToChi2Derivative.segment(
                srcRow * Acts::eAlignmentParametersSize,
                Acts::eAlignmentParametersSize);

        for (const auto& [colSurface, cols] : alignState.alignedSurfaces) {
          const auto& [dstCol, srcCol] = cols;
          sumChi2SecondDerivative.block<Acts::eAlignmentParametersSize,
                                        Acts::eAlignmentParametersSize>(
              dstRow * Acts::eAlignmentParametersSize,
              dstCol * Acts::eAlignmentParametersSize) +=
              alignState.alignmentToChi2SecondDerivative.block(
                  srcRow * Acts::eAlignmentParametersSize,
                  srcCol * Acts::eAlignmentParametersSize,
                  Acts::eAlignmentParametersSize,
                  Acts::eAlignmentParametersSize);
        }
      }
    }
    // Get the inverse of chi2 second derivative matrix
    // @Todo: use more stable method for solving the inverse
    Acts::ActsMatrixX<Acts::BoundParametersScalar>
        sumChi2SecondDerivativeInverse =
            Acts::ActsMatrixX<Acts::BoundParametersScalar>::Zero(
                alignResult.dof, alignResult.dof);
    sumChi2SecondDerivativeInverse = sumChi2SecondDerivative.inverse();
    if (sumChi2SecondDerivativeInverse.hasNaN()) {
      ACTS_ERROR("Chi2 second derivative inverse has NaN");
      return AlignmentError::AlignmentParametersUpdateFailure;
    }

    // Initialize the alignment results
    alignResult.deltaAlignmentParameters =
        Acts::ActsVectorX<Acts::BoundParametersScalar>::Zero(alignResult.dof);
    alignResult.alignmentCovariance =
        Acts::ActsMatrixX<Acts::BoundParametersScalar>::Zero(alignResult.dof,
                                                             alignResult.dof);
    // Solve the linear equation to get alignment parameters change
    alignResult.deltaAlignmentParameters =
        -sumChi2SecondDerivative.fullPivLu().solve(sumChi2Derivative);
    ACTS_VERBOSE("alignResult.deltaAlignmentParameters = \n "
                 << alignResult.deltaAlignmentParameters);
    // Alignment parameters covariance
    alignResult.alignmentCovariance = 2 * sumChi2SecondDerivativeInverse;
    // chi2 change
    alignResult.deltaChi2 = 0.5 * sumChi2Derivative.transpose() *
                            alignResult.deltaAlignmentParameters;
    // Update the aligned transform
    for (const auto& [surface, index] : idxedAlignSurfaces) {
      // (1) The original transform
      const Acts::Vector3D& oldCenter = surface->center(fitOptions.geoContext);
      const Acts::Transform3D& oldTransform =
          surface->transform(fitOptions.geoContext);
      const Acts::RotationMatrix3D& oldRotation = oldTransform.rotation();
      // The elements stored below is (rotZ, rotY, rotX)
      const Acts::Vector3D& oldEulerAngles = oldRotation.eulerAngles(2, 1, 0);

      // (2) The delta transform
      Acts::AlignmentVector deltaAlignmentParam =
          alignResult.deltaAlignmentParameters.segment(
              Acts::eAlignmentParametersSize, index);
      // The delta translation
      Acts::Vector3D deltaCenter =
          deltaAlignmentParam.segment<3>(Acts::eAlignmentCenter0);
      // The delta Euler angles
      Acts::Vector3D deltaEulerAngles =
          deltaAlignmentParam.segment<3>(Acts::eAlignmentRotation0);

      // (3) The new transform
      const Acts::Vector3D newCenter = oldCenter + deltaCenter;
      // The rotation around global z axis
      Acts::AngleAxis3D rotZ(oldEulerAngles(0) + deltaEulerAngles(2),
                             Acts::Vector3D::UnitZ());
      // The rotation around global y axis
      Acts::AngleAxis3D rotY(oldEulerAngles(1) + deltaEulerAngles(1),
                             Acts::Vector3D::UnitY());
      // The rotation around global x axis
      Acts::AngleAxis3D rotX(oldEulerAngles(2) + deltaEulerAngles(0),
                             Acts::Vector3D::UnitX());
      Acts::Rotation3D newRotation = rotZ * rotY * rotX;
      const Acts::Transform3D newTransform =
          Acts::Translation3D(newCenter) * newRotation;
      // Update the aligned transform
      //@Todo: use a better way to handle this(need dynamic cast to inherited
      // detector element type)
      bool updated = alignedTransformUpdater(
          alignedDetElements.at(index), fitOptions.geoContext, newTransform);
      if (not updated) {
        ACTS_ERROR("Update alignment parameters for detector element failed");
        return AlignmentError::AlignmentParametersUpdateFailure;
      }
    }

    return Acts::Result<void>::success();
  }

  /// @brief Alignment implementation
  ///
  /// @tparam trajectory_container_t The trajectories container type
  /// @tparam start_parameters_t The initial parameters container type
  /// @tparam fit_options_t The fit options type
  ///
  /// @param trajectoryCollection The collection of trajectories as input of
  /// fitting
  /// @param startParametersCollection The collection of starting parameters as
  /// input of fitting
  /// @param alignOptions The alignment options
  ///
  /// @result The alignment result
  template <typename trajectory_container_t,
            typename start_parameters_container_t, typename fit_options_t>
  Acts::Result<AlignmentResult> align(
      const trajectory_container_t& trajectoryCollection,
      const start_parameters_container_t& startParametersCollection,
      const AlignmentOptions<fit_options_t>& alignOptions) const {
    // Construct an AlignmentResult object
    AlignmentResult alignRes;

    // Start the iteration to minimize the chi2
    bool converged = false;
    for (unsigned int iIter = 0; iIter < alignOptions.maxIterations; iIter++) {
      // Perform the fit to the trajectories and update alignment parameters
      auto updateRes = updateAlignmentParameters(
          trajectoryCollection, startParametersCollection,
          alignOptions.fitOptions, alignOptions.alignedDetElements,
          alignOptions.alignedTransformUpdater, alignRes);
      if (not updateRes.ok()) {
        ACTS_ERROR("Update alignment parameters failed: " << updateRes.error());
        return updateRes.error();
      }
      ACTS_VERBOSE("iIter = " << iIter
                              << ", deltaChi2 = " << alignRes.deltaChi2);
      // Check if it has converged against the provided precision
      if (std::abs(alignRes.deltaChi2) <= alignOptions.deltaChi2CutOff) {
        converged = true;
        break;
      }
    }
    // Alignment failure if not converged
    if (not converged) {
      ACTS_ERROR("Alignment is not converged.");
      alignRes.result = AlignmentError::ConvergeFailure;
    }

    return alignRes;
  }

 private:
  // The fitter
  fitter_t m_fitter;

  /// Logger getter to support macros
  const Acts::Logger& logger() const { return *m_logger; }

  /// Owned logging instance
  std::shared_ptr<const Acts::Logger> m_logger;
};
}  // namespace FW