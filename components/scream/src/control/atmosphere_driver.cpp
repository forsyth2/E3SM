#include "control/atmosphere_driver.hpp"

#include "share/atm_process/atmosphere_process_group.hpp"
#include "share/atm_process/atmosphere_process_dag.hpp"
#include "share/field/field_utils.hpp"

#include "ekat/ekat_assert.hpp"
#include "ekat/util/ekat_string_utils.hpp"

namespace scream {

namespace control {

/*
 * IMPORTANT: read carefully this banner before attempting any change to the initialize method!
 *
 * The order in which the AD initializes all its internal stuff matters. Here's the order in
 * which operation currently happen, and why. If you alter the method, then a) make sure you
 * are not breaking any logic here explained (or else fix it!), and b) modify this banner to
 * update the explanation of the initialization sequence.
 *
 *  1) Create all atm processes. Each proc is allowed to start some sort of setup during creation,
 *     but will not be able to fully set up its required/computed fields, due to lack of grids info.
 *     However, and this is important, each process MUST establish what grid it needs.
 *  2) Create the grid manager, and query the atm procs for the grids they need. The GM will then
 *     proceed to build those grids (and only those grids).
 *  3) The GM is passed back to the atm procs, which can grab the needed grids, from which they can
 *     get the information needed to complete the setup of the FieldIdentifiers of their fields
 *     (both required and computed). Their field identifiers MUST be completed upon return from
 *     the 'set_grids' method.
 *     Note: at this stage, atm procs that act on non-ref grid(s) should be able to create their
 *           remappers. The AD will *not* take care of remapping inputs/outputs of the process.
 *  4) Register all fields from all atm procs inside the field manager (or field repo, whatever you
 *     want to call it).
 *  5) Set all the fields into the atm procs. Before this point, all the atm procs had were the
 *     FieldIdentifiers for their input/output fields. Now, we pass actual Field objects to them,
 *     where both the data (Kokkos::View) and metadata (FieldHeader) inside will be shared across
 *     all processes using the field. This allow data and metadata to be always in sync.
 *     Note: output fields are passed to an atm proc as read-write (i.e., non-const data type),
 *           while input fields are passed as read-only (i.e., const data type). Yes, the atm proc
 *           could cheat, and cast away the const, but we can't prevent that. However, in debug builds,
 *           we store 2 copies of each field, and use the extra copy to check, at run time, that
 *           no process alters the values of any of its input fields.
 *  6) All the atm inputs (that the AD can deduce by asking the atm proc group for the required fiedls)
 *     are initialized, by reading values from an initial conditions netcdf file.
 *     If an atm input is not found in the IC file, we'll error out, saving a DAG of the
 *     atm processes, which the user can inspect (to see what's missing in the IC file).
 *  7) All the atm process are initialized. During this call, atm process are able to set up
 *     all the internal structures that they were not able to init previously. They can also
 *     utilize their input fields to perform initialization of some internal data structure.
 *  8) Finally, set the initial time stamp on all fields, and perform some debug structure setup.
 *
 */

AtmosphereDriver::
AtmosphereDriver(const ekat::Comm& atm_comm,
                 const ekat::ParameterList& params)
{
  set_comm(atm_comm);
  set_params(params);
}

void AtmosphereDriver::
set_comm(const ekat::Comm& atm_comm)
{
  // I can't think of a scenario where changing the comm is a good idea,
  // so let's forbid it, for now.
  check_ad_status (s_comm_set, false);

  m_atm_comm = atm_comm;

  m_ad_status |= s_comm_set;
}

void AtmosphereDriver::
set_params(const ekat::ParameterList& atm_params)
{
  // I can't think of a scenario where changing the params is useful,
  // so let's forbid it, for now.
  check_ad_status (~s_params_set);

  m_atm_params = atm_params;

  m_ad_status |= s_params_set;
}

void AtmosphereDriver::create_atm_processes()
{
  // At this point, must have comm and params set.
  check_ad_status(s_comm_set | s_params_set);

  // Create the group of processes. This will recursively create the processes
  // tree, storing also the information regarding parallel execution (if needed).
  // See AtmosphereProcessGroup class documentation for more details.
  m_atm_process_group = std::make_shared<AtmosphereProcessGroup>(m_atm_comm,m_atm_params.sublist("Atmosphere Processes"));

  m_ad_status |= s_procs_created;
}

void AtmosphereDriver::create_grids()
{
  // Must have procs created by now (and comm/params set)
  check_ad_status (s_procs_created | s_comm_set | s_params_set);

  // Create the grids manager
  auto& gm_params = m_atm_params.sublist("Grids Manager");
  const std::string& gm_type = gm_params.get<std::string>("Type");
  m_grids_manager = GridsManagerFactory::instance().create(gm_type,m_atm_comm,gm_params);

  // Tell the grid manager to build all the grids required
  // by the atm processes, as well as the reference grid
  m_grids_manager->build_grids(m_atm_process_group->get_required_grids(),
                               gm_params.get<std::string>("Reference Grid"));

  // Set the grids in the processes. Do this by passing the grids manager.
  // Each process will grab what they need
  m_atm_process_group->set_grids(m_grids_manager);

  m_ad_status |= s_procs_created;
}

void AtmosphereDriver::create_fields()
{
  // Must have grids and procs at this point
  check_ad_status (s_procs_created | s_grids_created);

  // By now, the processes should have fully built the ids of their
  // required/computed fields. Let them register them in the repo
  m_field_repo = std::make_shared<FieldRepository<Real> >();
  m_field_repo->registration_begins();
  m_atm_process_group->register_fields(*m_field_repo);
  register_groups();
  m_field_repo->registration_ends(m_grids_manager);

  m_ad_status |= s_fields_created;
}

void AtmosphereDriver::initialize_output_manager () {
  check_ad_status (s_comm_set | s_params_set | s_grids_created | s_fields_created);

  // Create Initial conditions

  // Create Output manager
  if (m_atm_params.isSublist("Output Manager")) {
    auto& out_params = m_atm_params.sublist("Output Manager");
    m_output_manager.set_params(out_params);
    m_output_manager.set_comm(m_atm_comm);
    m_output_manager.set_grids(m_grids_manager);
    m_output_manager.set_repo(m_field_repo);
  }
  m_output_manager.init();

  m_ad_status |= s_output_inited;
}

void AtmosphereDriver::
initialize_fields (const util::TimeStamp& t0)
{
  // See if we need to print a DAG. We do this first, cause if any input
  // field is missing from the initial condition file, an error will be thrown.
  // By printing the DAG first, we give the user the possibility of seeing
  // what fields are inputs to the atm time step, so he/she can fix the i.c. file.

  auto& deb_pl = m_atm_params.sublist("Debug");
  const int verb_lvl = deb_pl.get<int>("Atmosphere DAG Verbosity Level",-1);
  if (verb_lvl>0) {
    // Check the atm DAG for missing stuff
    AtmProcDAG dag;

    // First, add all atm processes
    dag.create_dag(*m_atm_process_group,m_field_repo);

    // Then, add all surface coupling dependencies, if any
    if (m_surface_coupling) {
      dag.add_surface_coupling(m_surface_coupling->get_import_fids(),
                               m_surface_coupling->get_export_fids());
    }

    // Write a dot file for visualization
    dag.write_dag("scream_atm_dag.dot",std::max(verb_lvl,0));
  }

  // Figure out the list of inputs for the atmosphere.
  auto fields_in = m_atm_process_group->get_required_fields();

  const auto& ic_pl = m_atm_params.sublist("Initial Conditions");

  // Create parameter list for AtmosphereInput
  ekat::ParameterList ic_reader_params;
  ic_reader_params.set("GRID",m_grids_manager->get_reference_grid()->name());
  auto& ic_fields = ic_reader_params.sublist("FIELDS");
  int ifield=0;
  for (auto& fid : fields_in) {
    const auto& name = fid.name();
    auto f = m_field_repo->get_field(fid);
    // First, check if the input file contains constant values for some of the fields
    if (ic_pl.isParameter(name)) {
      // The user provided a constant value for this field. Simply use that.
      initialize_one_field<double>(name, ic_pl);
    } else {
      // The field does not have a constant value, so we expect to find it in the nc file
      ic_fields.set(ekat::strint("field",ifield+1),name); 
      ++ifield;

      // While at it, set the time stamp of the loaded fields to t0
    }
    f.get_header().get_tracking().update_time_stamp(t0);
  }

  if (ifield>0) {
    // There are fields to read from the nc file. We must have a valid nc file then.
    ic_reader_params.set("FILENAME",ic_pl.get<std::string>("Initial Conditions File"));
    ic_fields.set("Number of Fields",ifield);

    MPI_Fint fcomm = MPI_Comm_c2f(m_atm_comm.mpi_comm());
    if (!scorpio::is_eam_pio_subsystem_inited()) {
      scorpio::eam_init_pio_subsystem(fcomm);
    } else {
      EKAT_REQUIRE_MSG (fcomm==scorpio::eam_pio_subsystem_comm(),
          "Error! EAM subsystem was inited with a comm different from the current atm comm.\n");
    }

    AtmosphereInput ic_reader(m_atm_comm,ic_reader_params,m_field_repo,m_grids_manager);

    ic_reader.pull_input();
  }

  m_current_ts = t0;

  m_ad_status |= s_fields_inited;
}

template<typename T>
void AtmosphereDriver::initialize_one_field(const std::string& name, const ekat::ParameterList& ic_pl)
{
  printf("ASD - name = %s\n",name.c_str());  //ASD - DELETE
  auto f = m_field_repo->get_field(name,m_grids_manager->get_reference_grid()->name());
  // The user provided a constant value for this field. Simply use that.
  const auto& layout = f.get_header().get_identifier().get_layout();

  // For vector fields, we expect something like "fname: [val0,...,valN],
  // where the field dim is N+1. For scalars, "fname: val". So check the
  // field layout first, so we know what to get from the parameter list.
  if (layout.is_vector_layout()) {
    const auto idim = layout.get_vector_dim();
    const auto vec_dim = layout.dim(idim);
    const auto& values = ic_pl.get<std::vector<T>>(name);
    EKAT_REQUIRE_MSG (values.size()==static_cast<size_t>(vec_dim),
        "Error! Initial condition values array for '" + name + "' has the wrong dimension.\n"
        "       Field dimension: " + std::to_string(vec_dim) + "\n"
        "       Array dimenions: " + std::to_string(values.size()) + "\n");

    // Extract a subfield for each component. This is not "too" expensive, expecially
    // considering that this code is executed during initialization only.
    for (int comp=0; comp<vec_dim; ++comp) {
      auto f_i = f.get_component(comp);
      f_i.set_value(values[comp]);
      printf("    - [%d] = %f\n",comp,values[comp]);    // ASD-DELETE
    }
  } else {
    const auto& value = ic_pl.get<T>(name);
    printf("    - val = %f\n",value);  //ASD - DELETE
    f.set_value(value);
  }
}

void AtmosphereDriver::initialize_atm_procs ()
{
  // Set all the fields in the processes needing them (before, they only had ids)
  // Input fields will be handed to the processes as const
  const auto& inputs  = m_atm_process_group->get_required_fields();
  const auto& outputs = m_atm_process_group->get_computed_fields();
  for (const auto& id : inputs) {
    m_atm_process_group->set_required_field(m_field_repo->get_field(id).get_const());
  }
  // Output fields are handed to the processes as writable
  for (const auto& id : outputs) {
    m_atm_process_group->set_computed_field(m_field_repo->get_field(id));
  }
  // Set all groups of fields
  for (const auto& it : m_atm_process_group->get_required_groups()) {
    auto group = m_field_repo->get_const_field_group(it.name,it.grid);
    m_atm_process_group->set_required_group(group);
  }
  for (const auto& it : m_atm_process_group->get_updated_groups()) {
    auto group = m_field_repo->get_field_group(it.name,it.grid);
    m_atm_process_group->set_updated_group(group);
  }

  // Initialize the processes
  m_atm_process_group->initialize(m_current_ts);

  m_ad_status |= s_procs_inited;
}

void AtmosphereDriver::finish_setup ()
{
#ifdef SCREAM_DEBUG
  // In debug mode, we create a bkp field repo. We'll use it for a
  // very scrupolous check, to ensure atm procs don't update fields
  // that they were not entitled to update.
  m_bkp_field_repo.registration_begins();
  for (const auto& it : *m_field_repo) {
    for (const auto& id_field : it.second) {
      const auto& id = id_field.first;
      const auto& f = id_field.second;
      const auto& groups = f.get_header().get_tracking().get_groups_names();
      // Unfortunately, set<string> and set<CaseInsensitiveString>
      // are unrelated types for the compiler
      std::set<std::string> grps;
      for (const auto& group : groups) {
        grps.insert(group);
      }
      m_bkp_field_repo.register_field(id,grps);
    }
  }
  m_bkp_field_repo.registration_ends();

  // Deep copy the fields
  for (const auto& it : *m_field_repo) {
    for (const auto& id_field : it.second) {
      const auto& id = id_field.first;
      const auto& f  = id_field.second;
      auto src = f.get_view();
      auto tgt = m_bkp_field_repo.get_field(id).get_view();

      Kokkos::deep_copy(tgt,src);
    }
  }
  m_atm_process_group->set_field_repos(*m_field_repo,m_bkp_field_repo);
#endif
}

void AtmosphereDriver::initialize (const ekat::Comm& atm_comm,
                                   const ekat::ParameterList& params,
                                   const util::TimeStamp& t0)
{
  set_comm(atm_comm);
  set_params(params);

  create_atm_processes ();

  create_grids ();

  create_fields ();

  initialize_fields (t0);

  initialize_output_manager ();

  initialize_atm_procs ();

  finish_setup ();
}

void AtmosphereDriver::run (const Real dt) {
  // Make sure the end of the time step is after the current start_time
  EKAT_REQUIRE_MSG (dt>0, "Error! Input time step must be positive.\n");

  if (m_surface_coupling) {
    // Import fluxes from the component coupler (if any)
    m_surface_coupling->do_import();
  }

  // The class AtmosphereProcessGroup will take care of dispatching arguments to
  // the individual processes, which will be called in the correct order.
  m_atm_process_group->run(dt);

  // Update current time stamps
  m_current_ts += dt;

  // Update output streams
  m_output_manager.run(m_current_ts);

  if (m_surface_coupling) {
    // Export fluxes from the component coupler (if any)
    m_surface_coupling->do_export();
  }
}

void AtmosphereDriver::finalize ( /* inputs? */ ) {
  m_atm_process_group->finalize( /* inputs ? */ );

  // Finalize output streams, make sure files are closed
  m_output_manager.finalize();

  m_field_repo->clean_up();
#ifdef SCREAM_DEBUG
  m_bkp_field_repo.clean_up();
#endif

  if (scorpio::is_eam_pio_subsystem_inited()) {
    scorpio::eam_pio_finalize();
  }
}

void AtmosphereDriver::register_groups () {
  using GroupRequest = AtmosphereProcess::GroupRequest;

  // Given a list of group-grid pairs (A,B), make sure there is a copy
  // of each field in group A on grid B registered in the repo.
  auto has_group_fields_on_grid = [&](const std::set<GroupRequest>& groups_grids) {
    const auto& groups_info = m_field_repo->get_groups_info();

    for (const auto& gg : groups_grids) {
      const auto& group = gg.name;
      const auto& grid = gg.grid;

      // Lambda helper fcn, that register field $name with group $group on grid $grid
      // if not yet already registered
      auto register_if_not_there = [&](const std::string& name) {
        EKAT_REQUIRE_MSG(m_field_repo->has_field(name),
          "Error! Something went wrong while looking for field '" << name << "' in the repo.\n");
        auto aliases_begin = m_field_repo->cbegin(name);
        auto aliases_end = m_field_repo->cend(name);

        // Check if a copy of this field on the right grid is already registered.
        bool found = false;
        for (auto it = aliases_begin; it!=aliases_end; ++it) {
          if (it->get_grid_name()==grid) {
            found = true;
            break;
          }
        }

        if (!found) {
          // Field $name in group $group has no copy on grid $grid.
          // Lets take any fid in the repo for this field, and register
          // a copy of it on grid $grid. We can do this by creating
          // a remapper and using its capabilities.
          const auto& fid = *aliases_begin;
          auto r = m_grids_manager->create_remapper(fid.get_grid_name(),grid);
          auto f_units = fid.get_units();
          auto src_layout = fid.get_layout();
          auto tgt_layout = r->create_tgt_layout(src_layout);
          FieldIdentifier new_fid(name,tgt_layout,f_units,grid);
          m_field_repo->register_field(new_fid,gg.pack_size,group);
        }
      };

      auto group_it = groups_info.find(group);
      EKAT_REQUIRE_MSG(group_it!=groups_info.end(),
        "Error! Group '" << group << "' not found in the repo.\n");

      const auto& fnames = group_it->second->m_fields_names;
      for (const auto& name : fnames) {
        register_if_not_there(name);
      }

      if (group_it->second->m_bundled) {
        // The group was allocated as a single bundled field, with each
        // field in the group later subviewing the bundle.
        // We need to ensure the bundle also exists on $grid
        const auto& name = *fnames.begin();
        const auto  f = m_field_repo->get_field(name,grid);
        const auto& bundle_name = f.get_header().get_parent().lock()->get_identifier().name();
        register_if_not_there(bundle_name);
      }
    }
  };

  // Call the above lambda on both required and updated groups.
  has_group_fields_on_grid( m_atm_process_group->get_required_groups() );
  has_group_fields_on_grid( m_atm_process_group->get_updated_groups() );
}

#ifdef SCREAM_DEBUG
void AtmosphereDriver::create_bkp_field_repo () {
  m_bkp_field_repo.registration_begins();
  for (const auto& it : *m_field_repo) {
    for (const auto& id_field : it.second) {
      const auto& id = id_field.first;
      const auto& f = id_field.second;
      const auto& groups = f.get_header().get_tracking().get_groups_names();
      // Unfortunately, set<string> and set<CaseInsensitiveString>
      // are unrelated types for the compiler
      std::set<std::string> grps;
      for (const auto& group : groups) {
        grps.insert(group);
      }
      m_bkp_field_repo.register_field(id,grps);
    }
  }
  m_bkp_field_repo.registration_ends();

}
#endif

void AtmosphereDriver::
check_ad_status (const int flag, const bool must_be_set)
{
  if (must_be_set) {
    EKAT_REQUIRE_MSG (m_ad_status & flag,
        "Error! Failed AD status check:\n"
        "        expected flag:  " + std::to_string(flag) + "\n"
        "        ad status flag: " + std::to_string(m_ad_status) + "\n");
  } else {
    EKAT_REQUIRE_MSG (~m_ad_status & flag,
        "Error! Failed AD status check:\n"
        "        not expected flag:  " + std::to_string(flag) + "\n"
        "        ad status flag: " + std::to_string(m_ad_status) + "\n");
  }
}

}  // namespace control
}  // namespace scream
