#ifndef DP_ADVANCE_IOP_NUDGING_IMPL_HPP
#define DP_ADVANCE_IOP_NUDGING_IMPL_HPP

#include "dp_functions.hpp" // for ETI only but harmless for GPU

namespace scream {
namespace dp {

/*
 * Implementation of dp advance_iop_nudging. Clients should NOT
 * #include this file, but include dp_functions.hpp instead.
 */

template<typename S, typename D>
KOKKOS_FUNCTION
void Functions<S,D>::advance_iop_nudging(const Int& plev, const Spack& scm_dt, const Spack& ps_in, const uview_1d<const Spack>& t_in, const uview_1d<const Spack>& q_in, const uview_1d<Spack>& t_update, const uview_1d<Spack>& q_update, const uview_1d<Spack>& relaxt, const uview_1d<Spack>& relaxq)
{
  // TODO
  // Note, argument types may need tweaking. Generator is not always able to tell what needs to be packed
}

} // namespace dp
} // namespace scream

#endif
