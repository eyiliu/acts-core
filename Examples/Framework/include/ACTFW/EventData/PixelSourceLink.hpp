// This file is part of the Acts project.
//
// Copyright (C) 2020 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <stdexcept>
#include <string>

#include "Acts/EventData/Measurement.hpp"

namespace FW {

/// Source link class for Alpide pixel hit.
///
/// The source link stores the measuremts, surface
///
class PixelSourceLink {
 public:
  PixelSourceLink(const Acts::Surface& surface, size_t dim,
                  Acts::Vector2D values, Acts::ActsSymMatrixD<2> cov)
      : m_values(values), m_cov(cov), m_dim(dim), m_surface(&surface) {}
  /// Must be default_constructible to satisfy SourceLinkConcept.
  PixelSourceLink() = default;
  PixelSourceLink(PixelSourceLink&&) = default;
  PixelSourceLink(const PixelSourceLink&) = default;
  PixelSourceLink& operator=(PixelSourceLink&&) = default;
  PixelSourceLink& operator=(const PixelSourceLink&) = default;

  constexpr const Acts::Surface& referenceSurface() const { return *m_surface; }

  Acts::FittableMeasurement<PixelSourceLink> operator*() const {
    if (m_dim != 2) {
      throw std::runtime_error("Dim " + std::to_string(m_dim) +
                               " currently not supported.");
    }
    return Acts::Measurement<PixelSourceLink, Acts::ParDef::eLOC_0,
                             Acts::ParDef::eLOC_1>{
        m_surface->getSharedPtr(), *this, m_cov, m_values[0], m_values[1]};
  }

 private:
  Acts::BoundVector m_values;
  Acts::BoundMatrix m_cov;
  size_t m_dim = 0u;
  // need to store pointers to make the object copyable
  const Acts::Surface* m_surface;
  friend constexpr bool operator==(const PixelSourceLink& lhs,
                                   const PixelSourceLink& rhs) {
    return lhs.m_values == rhs.m_values;
  }
};

}  // end of namespace FW
