#ifndef SCREAM_COMMON_PHYSICS_IMPL_HPP
#define SCREAM_COMMON_PHYSICS_IMPL_HPP

#include "share/util/scream_common_physics_functions.hpp"
#include "physics/share/physics_constants.hpp"

namespace scream {

//-----------------------------------------------------------------------------------------------//
// Computes exner function
// The result is exners formula, and is [dimensionless]
// The input is mid-level pressure, and has units of [Pa]
template<typename DeviceT>
template<typename ScalarT>
KOKKOS_INLINE_FUNCTION
ScalarT PhysicsFunctions<DeviceT>::exner_function(const ScalarT& pressure)
{
  using C = scream::physics::Constants<Real>;

  static constexpr auto p0 = C::P0;
  static constexpr auto rd = C::RD;
  static constexpr auto inv_cp = C::INV_CP;

  return pow( pressure/p0, rd*inv_cp );
}
// For operations on a full column at a time:
template<typename DeviceT>
template<typename ScalarT, typename InputProviderP>
KOKKOS_INLINE_FUNCTION
void PhysicsFunctions<DeviceT>::exner_function(const MemberType& team,
                                               const InputProviderP& pressure,
                                               const view_1d<ScalarT>& exner)
{
  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,exner.extent(0)),
                       [&] (const int k) {
    exner(k) = exner_function(pressure(k));
  });
}

//-----------------------------------------------------------------------------------------------//
// Converts temperature into potential temperature
// The result is the potential temperature, units in [K]
// The inputs are
//   temperature is the atmospheric temperature, units in [K]
//   pressure is the atmospheric pressure, units in [Pa].  pressure is used in Exners function using `exner` defined above.
template<typename DeviceT>
template<typename ScalarT>
KOKKOS_INLINE_FUNCTION
ScalarT PhysicsFunctions<DeviceT>::calculate_theta_from_T(const ScalarT& temperature, const ScalarT& pressure)
{
  // Theta = T/exner;
  return temperature/exner_function(pressure);
}

// For operations on a full column at a time:
template<typename DeviceT>
template<typename ScalarT, typename InputProviderT, typename InputProviderP>
KOKKOS_INLINE_FUNCTION
void PhysicsFunctions<DeviceT>::calculate_theta_from_T(const MemberType& team,
                                                       const InputProviderT& temperature,
                                                       const InputProviderP& pressure,
                                                       const view_1d<ScalarT>& theta)
{
  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,theta.extent(0)),
                       [&] (const int k) {
    theta(k) = calculate_theta_from_T(temperature(k),pressure(k));
  });
}

//-----------------------------------------------------------------------------------------------//
// Converts potential temperature into temperature
// The result is the temperature, units in [K]
// The inputs are
//   theta is the potential temperature, units in [K]
//   pressure is the atmospheric pressure, units in [Pa].  pressure is used in Exners function using `exner` defined above.
template<typename DeviceT>
template<typename ScalarT>
KOKKOS_INLINE_FUNCTION
ScalarT PhysicsFunctions<DeviceT>::calculate_T_from_theta(const ScalarT& theta, const ScalarT& pressure)
{
  // T = Theta*exner
  return theta*exner_function(pressure);
}

// For operations on a full column at a time:
template<typename DeviceT>
template<typename ScalarT, typename InputProviderT, typename InputProviderP>
KOKKOS_INLINE_FUNCTION
void PhysicsFunctions<DeviceT>::calculate_T_from_theta(const MemberType& team,
                                                       const InputProviderT& theta,
                                                       const InputProviderP& pressure,
                                                       const view_1d<ScalarT>& temperature)
{
  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,temperature.extent(0)),
                       [&] (const int k) {
    temperature(k) = calculate_T_from_theta(theta(k),pressure(k));
  });
}
//-----------------------------------------------------------------------------------------------//
// Compute temperature from virtual temperature
// The result unit is in [K]
// The inputs are
//   T_virtual is the virtual temperature.  Units in [K].
//   qv        is the water vapor mass mixing ratio.  Units in [kg/kg]
template<typename DeviceT>
template<typename ScalarT>
KOKKOS_INLINE_FUNCTION
ScalarT PhysicsFunctions<DeviceT>::
calculate_temperature_from_virtual_temperature(const ScalarT& T_virtual, const ScalarT& qv)
{
  using C = scream::physics::Constants<Real>;

  static constexpr auto ep_2 = C::ep_2;
  return T_virtual*((ep_2*(1.0+qv))/(qv+ep_2));
}

template<typename DeviceT>
template<typename ScalarT, typename InputProviderT, typename InputProviderQ>
KOKKOS_INLINE_FUNCTION
void PhysicsFunctions<DeviceT>::
calculate_temperature_from_virtual_temperature(const MemberType& team,
                                               const InputProviderT& T_virtual,
                                               const InputProviderQ& qv,
                                               const view_1d<ScalarT>& temperature)
{
  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,temperature.extent(0)),
                       [&] (const int k) {
    temperature(k) = calculate_temperature_from_virtual_temperature(T_virtual(k),qv(k));
  });
}


//-----------------------------------------------------------------------------------------------//
// Compute virtual temperature
// The result unit is in [K]
// The inputs are
//   temperature is the atmospheric temperature.  Units in [K].
//   qv    is the water vapor mass mixing ratio.  Units in [kg/kg]
template<typename DeviceT>
template<typename ScalarT>
KOKKOS_INLINE_FUNCTION
ScalarT PhysicsFunctions<DeviceT>::calculate_virtual_temperature(const ScalarT& temperature, const ScalarT& qv)
{
  using C = scream::physics::Constants<Real>;

  static constexpr auto ep_2 = C::ep_2;
  return temperature*((qv+ep_2)/(ep_2*(1.0+qv)));
}

template<typename DeviceT>
template<typename ScalarT, typename InputProviderT, typename InputProviderQ>
KOKKOS_INLINE_FUNCTION
void PhysicsFunctions<DeviceT>::
calculate_virtual_temperature(const MemberType& team,
                              const InputProviderT& temperature,
                              const InputProviderQ& qv,
                              const view_1d<ScalarT>& T_virtual)
{
  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,T_virtual.extent(0)),
                       [&] (const int k) {
    T_virtual(k) = calculate_virtual_temperature(temperature(k),qv(k));
  });
}

//-----------------------------------------------------------------------------------------------//
// Compute dry static energy (DSE).
// The result unit is in [J/kg]
// The inputs are
//   temperature       is the atmospheric temperature. Units in [K].
//   z                 is the geopotential height above surface at midpoints. Units in [m].
//   surf_geopotential is the surface geopotential height. Units in [m].
template<typename DeviceT>
template<typename ScalarT>
KOKKOS_INLINE_FUNCTION
ScalarT PhysicsFunctions<DeviceT>::
calculate_dse(const ScalarT& temperature, const ScalarT& z, const Real surf_geopotential)
{
  using C = scream::physics::Constants<Real>;

  static constexpr auto cp = C::CP;
  static constexpr auto g  = C::gravit;

  return cp*temperature + g*z + surf_geopotential;
}

template<typename DeviceT>
template<typename ScalarT, typename InputProviderT, typename InputProviderZ>
KOKKOS_INLINE_FUNCTION
void PhysicsFunctions<DeviceT>::calculate_dse(const MemberType& team,
                                              const InputProviderT& temperature,
                                              const InputProviderZ& z,
                                              const Real surf_geopotential,
                                              const view_1d<ScalarT>& dse)
{
  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,dse.extent(0)),
                       [&] (const int k) {
    dse(k) = calculate_dse(temperature(k),z(k),surf_geopotential);
  });
}
//-----------------------------------------------------------------------------------------------//
// Determine the physical thickness of a vertical layer
// The result is dz, units in [m]
// The inputs are
//   pseudo_density is the pressure level thickness, [Pa]
//   p_mid          is the average atmosphere pressure over the level, [Pa]
//   T_mid          is the average atmospheric temperature over the level, [K] - needed for T_virtual
//   qv             is the water vapor mass mixing ratio, [kg/kg] - needed for T_virtual
template<typename DeviceT>
template<typename ScalarT>
KOKKOS_INLINE_FUNCTION
ScalarT PhysicsFunctions<DeviceT>::
calculate_dz(const ScalarT& pseudo_density, const ScalarT& p_mid, const ScalarT& T_mid, const ScalarT& qv)
{
  // dz = pseudo_density*T_v*R/(p*g)
  using C = scream::physics::Constants<Real>;

  const ScalarT& T_virtual = calculate_virtual_temperature(T_mid,qv);

  static constexpr auto rd = C::RD;
  static constexpr auto g  = C::gravit;
  return (rd/g)*pseudo_density*T_virtual / p_mid;
}

template<typename DeviceT>
template<typename ScalarT,
         typename InputProviderPD, typename InputProviderP,
         typename InputProviderT,  typename InputProviderQ>
KOKKOS_INLINE_FUNCTION
void PhysicsFunctions<DeviceT>::calculate_dz(const MemberType& team,
                                             const InputProviderPD& pseudo_density,
                                             const InputProviderP& p_mid,
                                             const InputProviderT& T_mid,
                                             const InputProviderQ& qv,
                                             const view_1d<ScalarT>& dz)
{
  Kokkos::parallel_for(Kokkos::TeamThreadRange(team,dz.extent(0)),
                       [&] (const int k) {
    dz(k) = calculate_dz(pseudo_density(k),p_mid(k),T_mid(k),qv(k));
  });
}
//-----------------------------------------------------------------------------------------------//
// Determine the geopotential height of level interfaces
// The result is z_int, units in [m]
// The inputs are
//   dz the vertical level thickness, [m]
//   z_surf: the surface elevation [m]
// Note: Only applicable over an entire column due to the need to integrate over dz.
template<typename DeviceT>
template<typename ScalarT, typename InputProviderZ>
KOKKOS_INLINE_FUNCTION
void PhysicsFunctions<DeviceT>::calculate_z_int(const MemberType& team,
                                                const int num_levs,
                                                const InputProviderZ& dz,
                                                const Real z_surf,
                                                const view_1d<ScalarT>& z_int)
{
  using column_ops  = ColumnOps<DeviceT,Real>;
  // Note, we set FromTop to false since we are prescribing the *bottom* elevation.
  constexpr bool FromTop = false;
  column_ops::template column_scan<FromTop>(team,num_levs,dz,z_int,z_surf);
}
//-----------------------------------------------------------------------------------------------//
} // namespace scream

#endif // SCREAM_COMMON_PHYSICS_IMPL_HPP
