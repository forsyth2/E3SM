#ifndef SCREAM_COARSENING_REMAPPER_HPP
#define SCREAM_COARSENING_REMAPPER_HPP

#include "share/grid/remap/abstract_remapper.hpp"

#include "scream_config.h"

#include <mpi.h>

namespace scream
{

// An abstract interface for a remapper

// A remapper is basically a functor, that, given two fields,
// copies the first into the second, or viceversa. The copy must
// account for different layouts and/or different mpi distributions.
// This concept can be extended to remaps that involve interpolation,
// but as of now (07/2019) it is not the intent and projected use
// of this class in the scream framework
class CoarseningRemapper : public AbstractRemapper
{
public:

  CoarseningRemapper (const grid_ptr_type& src_grid,
                      // const grid_ptr_type& tgt_grid,
                      const std::string& map_file);

  ~CoarseningRemapper ();

  FieldLayout create_src_layout (const FieldLayout& tgt_layout) const override;
  FieldLayout create_tgt_layout (const FieldLayout& src_layout) const override;

  bool compatible_layouts (const layout_type& src,
                           const layout_type& tgt) const override {
    // Same type of layout, and same sizes except for possibly the first one
    return get_layout_type(src.tags())==get_layout_type(tgt.tags()) &&
           src.size()/src.dim(0) == tgt.size()/tgt.dim(0);
  }

protected:
  static grid_ptr_type create_tgt_grid (const std::string& map_file,
                                        const grid_ptr_type& src_grid);

  const identifier_type& do_get_src_field_id (const int ifield) const override {
    return m_src_fields[ifield].get_header().get_identifier();
  }
  const identifier_type& do_get_tgt_field_id (const int ifield) const override {
    return m_tgt_fields[ifield].get_header().get_identifier();
  }
  const field_type& do_get_src_field (const int ifield) const override {
    return m_src_fields[ifield];
  }
  const field_type& do_get_tgt_field (const int ifield) const override {
    return m_tgt_fields[ifield];
  }

  void do_registration_begins () override { /* Nothing to do here */ }

  void do_register_field (const identifier_type& src, const identifier_type& tgt) override;

  void do_bind_field (const int ifield, const field_type& src, const field_type& tgt) override;

  void do_registration_ends () override;

  void do_remap_fwd () const override;

  void do_remap_bwd () const  override {
    EKAT_ERROR_MSG ("CoarseningRemapper only supports fwd remapping.\n");
  }

protected:

  using KT = KokkosTypes<DefaultDevice>;
  using gid_t = AbstractGrid::gid_type;

  template<int N>
  using RPack = ekat::Pack<Real,N>;

  template<typename T>
  using view_1d = typename KT::template view_1d<T>;
  template<typename T>
  using view_2d = typename KT::template view_2d<T>;

  void create_ov_tgt_fields ();
  void setup_mpi_data_structures ();

  void pack () const;
  void unpack () const;

  view_1d<gid_t>::HostMirror
  get_my_triplets_gids (const std::string& map_file) const;

#ifdef KOKKOS_ENABLE_CUDA
public:
#endif
  template<int N>
  void local_mat_vec (const Field& f_src, const Field& f_tgt) const;

protected:
  ekat::Comm            m_comm;

  static constexpr bool MpiOnDev = SCREAM_MPI_ON_DEVICE;

  // If MpiOnDev=true, we can pass device pointers to MPI. Otherwise, we need host mirrors.
  template<typename T>
  using mpi_view_1d = typename std::conditional<
                        MpiOnDev,
                        view_1d<T>,
                        typename view_1d<T>::HostMirror
                      >::type;

  // An "overlapped" tgt grid, that is a version of the tgt grid where
  // ranks own all rows that are affected by local dofs in their src grid
  grid_ptr_type         m_ov_tgt_grid;

  // Source, target, and overlapped-target fields
  std::vector<Field>    m_src_fields;
  std::vector<Field>    m_ov_tgt_fields;
  std::vector<Field>    m_tgt_fields;

  // Sparse matrix representation in triplet form
  view_2d<int>    m_row_col_lids;
  view_1d<Real>   m_weights;

  //  ------- MPI data structures -------- //

  // The send/recv buf for pack/unpack
  view_1d<Real>         m_send_buffer;
  view_1d<Real>         m_recv_buffer;

  // The send/recv buf to feed to MPI. If MpiOnDev=true, they alias the ones aboce
  mpi_view_1d<Real>     m_mpi_send_buffer;
  mpi_view_1d<Real>     m_mpi_recv_buffer;

  // offset(I,J) = offset in send/recv buffor send/recv to/from pid J for field I
  view_2d<int>          m_send_f_pid_offsets;
  view_2d<int>          m_recv_f_pid_offsets; // DONE

  // Offset of each PID in the send/recv buffer
  view_1d<int>          m_send_pid_offsets;
  view_1d<int>          m_recv_pid_offsets; // DONE

  // Reorder the lids so that all lids to send/recv to/from pid N
  // come before those for pid N+1. Also,
  //   lids_pids(i,0): ith lid to send to pid=lids_pids(i,1)
  // Note: send/recv lids are the lids of gids in the ov_tgt/tgt grids.
  //       But here, dofs are ordered differently, so that all dofs to
  //       send/recv to/from the same pid are contiguous.
  view_2d<int>          m_send_lids_pids;
  view_2d<int>          m_recv_lids_pids; // DONE

  // Store the start of lids to send/recv to/from each pid in the views above
  // Note: these are different form m_[send|recv]_pid_offsets. Those are
  //       offsets in the full send/recv buffer, while these are offsets in
  //       the views above.
  view_1d<int>          m_send_pid_lids_start;
  view_1d<int>          m_recv_pid_lids_start; // DONE

  // Send/recv requests
  std::vector<MPI_Request>  m_recv_req;
  std::vector<MPI_Request>  m_send_req;

  // Unfortunately, MPI's startall/waitall needs a ptr to nonconst requests.
  // Since do_remap_fwd is a const method, calling m_recv_req.data() would
  // return a pointer to const. So store the nonconst ptrs.
  // Note: these are persistent requests, so startall/waitall should *not*
  //       change them (recall that MPI_Request is an opaque pointer).
  MPI_Request*              m_recv_req_ptr;
  MPI_Request*              m_send_req_ptr;

  // While the total number of gids in send ops matches the number of local
  // dofs in m_ov_tgt_grid, the total number of gids in recv can be (and usually
  // is) larger than the number of local dofs in m_tgt_grid. In fact, there
  // can be more than 1 pid computing a contribution for the same tgt gid.
  // Hence, we need to store this number
  int m_total_num_recv_gids;
};

} // namespace scream

#endif // SCREAM_COARSENING_REMAPPER_HPP
