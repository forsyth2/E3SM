#ifndef SCREAM_SHOC_FUNCTIONS_F90_HPP
#define SCREAM_SHOC_FUNCTIONS_F90_HPP

#include "ekat/util/scream_utils.hpp"
#include "ekat/scream_types.hpp"

#include "shoc_functions.hpp"

#include <vector>
#include <array>
#include <utility>

//
// Bridge functions to call fortran version of shoc functions from C++
//

namespace scream {
namespace shoc {

///////////////////////////////////////////////////////////////////////////////
// Converted subroutine helpers go here.
struct SHOCGridData {
  static constexpr size_t NUM_ARRAYS   = 4;
  static constexpr size_t NUM_ARRAYS_i = 2;

  // Inputs
  Int shcol, nlev, nlevi;
  Real *zt_grid, *zi_grid, *pdel;

  // In/out
  Real *dz_zt, *dz_zi, *rho_zt;

  SHOCGridData(Int shcol_, Int nevl_, Int nlevi_);
  SHOCGridData(const SHOCGridData &rhs);
  SHOCGridData &operator=(const SHOCGridData &rhs);

  void init_ptrs();

  // Internals
  Int m_shcol, m_nlev, m_nlevi, m_total, m_totali;
  std::vector<Real> m_data;
  std::vector<Real> m_datai;

  template <util::TransposeDirection::Enum D>
  void transpose() {
    SHOCGridData d_trans(*this);

    // Transpose on the zt grid
    util::transpose<D>(zt_grid, d_trans.zt_grid, shcol, nlev);
    util::transpose<D>(dz_zt, d_trans.dz_zt, shcol, nlev);
    util::transpose<D>(pdel, d_trans.pdel, shcol, nlev);
    util::transpose<D>(rho_zt, d_trans.rho_zt, shcol, nlev);

    // Transpose on the zi grid
    util::transpose<D>(zi_grid, d_trans.zi_grid, shcol, nlevi);
    util::transpose<D>(dz_zi, d_trans.dz_zi, shcol, nlevi);

    *this = std::move(d_trans);
  }
};

// This function initialzes the grid used by shoc. Given the 
// locations of the cell center (location of thermodynaics quantities), cell 
// interfaces, and pressure gradient the functon returns dz_zi, dz_zt,
// and density. 
void shoc_grid(Int nlev, SHOCGridData &d);


//Create data structure to hold data for calc_shoc_vertflux
struct SHOCVertfluxData {
  static constexpr size_t NUM_ARRAYS   = 1; //# of arrays with values at cell centers (zt grid)
  static constexpr size_t NUM_ARRAYS_i = 3; //# of arrays with values at interface centers (zi grid)

  // Inputs
  Int   shcol, nlev, nlevi;
  Real *tkh_zi, *dz_zi, *invar;

  // In/out
  Real *vertflux;

  //functions to initialize data
  SHOCVertfluxData(Int shcol_, Int nevl_, Int nlevi_);
  SHOCVertfluxData(const SHOCVertfluxData &rhs);
  SHOCVertfluxData &operator=(const SHOCVertfluxData &rhs);

  void init_ptrs();

  // Internals
  Int m_shcol, m_nlev, m_nlevi, m_total, m_totali;
  std::vector<Real> m_data;
  std::vector<Real> m_datai;

  template <util::TransposeDirection::Enum D>
  void transpose() {
    SHOCVertfluxData d_trans(*this);

    // Transpose on the zt grid
    util::transpose<D>(invar, d_trans.invar, shcol, nlev);

    // Transpose on the zi grid
    util::transpose<D>(tkh_zi, d_trans.tkh_zi, shcol, nlevi);
    util::transpose<D>(dz_zi, d_trans.dz_zi, shcol, nlevi);
    util::transpose<D>(vertflux, d_trans.vertflux, shcol, nlevi);

    *this = std::move(d_trans);
  }
};//SHOCVertfluxData

void calc_shoc_vertflux(Int nlev, SHOCVertfluxData &d);

struct SHOCVarorcovarData {
  static constexpr size_t NUM_ARRAYS   = 2;
  static constexpr size_t NUM_ARRAYS_i = 4;

  // Inputs
  Int   shcol, nlev, nlevi;
  Real tunefac;
  Real *tkh_zi, *dz_zi, *isotropy_zi, *invar1, *invar2;

  // In/out
  Real *varorcovar;

  SHOCVarorcovarData(Int shcol_, Int nlev_, Int nlevi_, Real tunefac_);
  SHOCVarorcovarData(const SHOCVarorcovarData &rhs);
  SHOCVarorcovarData &operator=(const SHOCVarorcovarData &rhs);

  void init_ptrs();

  // Internals
  Int m_shcol, m_nlev, m_nlevi, m_total, m_totali;
  std::vector<Real> m_data;
  std::vector<Real> m_datai;

  template <util::TransposeDirection::Enum D>
  void transpose() {
    SHOCVarorcovarData d_trans(*this);

    // Transpose on the zt grid
    util::transpose<D>(invar1, d_trans.invar1, shcol, nlev);
    util::transpose<D>(invar2, d_trans.invar2, shcol, nlev);

    // Transpose on the zi grid
    util::transpose<D>(tkh_zi, d_trans.tkh_zi, shcol, nlevi);
    util::transpose<D>(dz_zi, d_trans.dz_zi, shcol, nlevi);
    util::transpose<D>(isotropy_zi, d_trans.isotropy_zi, shcol, nlevi);
    util::transpose<D>(varorcovar, d_trans.varorcovar, shcol, nlevi);

    *this = std::move(d_trans);
  }
};//SHOCVarorcovarData

void calc_shoc_varorcovar(Int nlev, SHOCVarorcovarData &d);

}  // namespace shoc
}  // namespace scream

#endif
