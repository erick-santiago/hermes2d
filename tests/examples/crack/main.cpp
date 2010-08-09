#define H2D_REPORT_WARN
#define H2D_REPORT_INFO
#define H2D_REPORT_VERBOSE
#define H2D_REPORT_FILE "application.log"
#include "hermes2d.h"

using namespace RefinementSelectors;

// This test makes sure that example "crack" works correctly.

const int INIT_REF_NUM = 0;              // Number of initial uniform mesh refinements.
const int P_INIT = 2;                    // Initial polynomial degree of all mesh elements.
const bool MULTI = true;                 // true = use multi-mesh, false = use single-mesh.
                                         // Note: in the single mesh option, the meshes are
                                         // forced to be geometrically the same but the
                                         // polynomial degrees can still vary.
const double THRESHOLD_MULTI = 0.35;     // error threshold for element refinement (multi-mesh)
const double THRESHOLD_SINGLE = 0.7;     // error threshold for element refinement (single-mesh)
const int STRATEGY = 0;                  // Adaptive strategy:
                                         // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                         //   error is processed. If more elements have similar errors, refine
                                         //   all to keep the mesh symmetric.
                                         // STRATEGY = 1 ... refine all elements whose error is larger
                                         //   than THRESHOLD times maximum element error.
                                         // STRATEGY = 2 ... refine all elements whose error is larger
                                         //   than THRESHOLD.
                                         // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO; // Predefined list of element refinement candidates. Possible values are
                                         // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                         // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                         // See User Documentation for details.
const int MESH_REGULARITY = -1;          // Maximum allowed level of hanging nodes:
                                         // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                         // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                         // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                         // Note that regular meshes are not supported, this is due to
                                         // their notoriously bad performance.
const double CONV_EXP = 1.0;             // Default value is 1.0. This parameter influences the selection of
                                         // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
const double ERR_STOP = 0.5;             // Stopping criterion for adaptivity (rel. error tolerance between the
                                         // reference mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;             // Adaptivity process stops when the number of degrees of freedom grows.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_UMFPACK, SOLVER_PETSC,
                                                  // SOLVER_MUMPS, and more are coming.

// Problem parameters.
const double E  = 200e9;                 // Young modulus for steel: 200 GPa.
const double nu = 0.3;                   // Poisson ratio.
const double f  = 1e3;                   // Load force.
const double lambda = (E * nu) / ((1 + nu) * (1 - 2*nu));
const double mu = E / (2*(1 + nu));

// Boundary markers.
const int BDY_LEFT = 1;
const int BDY_TOP = 2;

// Boundary condition types.
BCType bc_types_xy(int marker)
  { return (marker == BDY_LEFT) ? BC_ESSENTIAL : BC_NATURAL; }

// Essential (Dirichlet) boundary condition values.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return 0;
}

// Weak forms.
#include "forms.cpp"

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh u_mesh, v_mesh;
  H2DReader mloader;
  mloader.load("crack.mesh", &u_mesh);

  // Perform initial uniform mesh refinement.
  for (int i=0; i < INIT_REF_NUM; i++) u_mesh.refine_all_elements();

  // Create initial mesh for the vertical displacement component.
  // This also initializes the multimesh hp-FEM.
  v_mesh.copy(&u_mesh);

  // Create H1 spaces with default shapesets.
  H1Space u_space(&u_mesh, bc_types_xy, essential_bc_values, P_INIT);
  H1Space v_space(MULTI ? &v_mesh : &u_mesh, bc_types_xy, essential_bc_values, P_INIT);

  // Initialize the weak formulation.
  WeakForm wf(2);
  wf.add_matrix_form(0, 0, callback(bilinear_form_0_0), H2D_SYM);
  wf.add_matrix_form(0, 1, callback(bilinear_form_0_1), H2D_SYM);
  wf.add_matrix_form(1, 1, callback(bilinear_form_1_1), H2D_SYM);
  wf.add_vector_form_surf(1, callback(linear_form_surf_1), BDY_TOP);

  // Initialize refinement selector.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // Initialize matrix solver.
  Matrix* mat; Vector* rhs; CommonSolver* solver;  
  init_matrix_solver(matrix_solver, u_space.get_num_dofs() + v_space.get_num_dofs(), 
                     mat, rhs, solver);

  // Adaptivity loop:
  Solution u_sln, v_sln, ref_u_sln, ref_v_sln;
  int as = 1; bool done = false;
  do
  {
    info("---- Adaptivity step %d:", as);
    info("Solving on reference mesh.");

    // Construct globally refined reference meshes.
    Mesh ref_u_mesh, ref_v_mesh;
    ref_u_mesh.copy(&u_mesh);
    ref_v_mesh.copy(&v_mesh);
    ref_u_mesh.refine_all_elements();
    ref_v_mesh.refine_all_elements();

    // Setup spaces for the reference solution.
    Space *ref_u_space = u_space.dup(&ref_u_mesh);
    Space *ref_v_space = v_space.dup(&ref_v_mesh);
    int order_increase = 1;
    ref_u_space->copy_orders(&u_space, order_increase);
    ref_v_space->copy_orders(&v_space, order_increase);
 
    // Solve the reference problem.
    solve_linear(Tuple<Space *>(ref_u_space, ref_v_space), &wf, 
                 Tuple<Solution *>(&ref_u_sln, &ref_v_sln), matrix_solver);

    // Project the reference solutions on the coarse meshes.
    info("Projecting reference solutions on coarse meshes.");
    project_global(Tuple<Space *>(&u_space, &v_space), 
                   Tuple<int>(H2D_H1_NORM, H2D_H1_NORM),
                   Tuple<MeshFunction*>(&ref_u_sln, &ref_v_sln), 
                   Tuple<Solution*>(&u_sln, &v_sln));

    // Calculate error estimate wrt. reference solution in energy norm.
    info("Calculating error (est).");
    Adapt hp(Tuple<Space *>(&u_space, &v_space), Tuple<int>(H2D_H1_NORM, H2D_H1_NORM));
    hp.set_solutions(Tuple<Solution*>(&u_sln, &v_sln), 
                     Tuple<Solution*>(&ref_u_sln, &ref_v_sln));
    hp.set_error_form(0, 0, bilinear_form_0_0<scalar, scalar>, bilinear_form_0_0<Ord, Ord>);
    hp.set_error_form(0, 1, bilinear_form_0_1<scalar, scalar>, bilinear_form_0_1<Ord, Ord>);
    hp.set_error_form(1, 0, bilinear_form_1_0<scalar, scalar>, bilinear_form_1_0<Ord, Ord>);
    hp.set_error_form(1, 1, bilinear_form_1_1<scalar, scalar>, bilinear_form_1_1<Ord, Ord>);
    double err_est = hp.calc_elem_errors(H2D_TOTAL_ERROR_REL | H2D_ELEMENT_ERROR_REL) * 100;

    // Report results.
    info("u_ndof: %d, ref_u_ndof: %d", u_space.get_num_dofs(), ref_u_space->get_num_dofs());
    info("v_ndof: %d, ref_v_ndof: %d", v_space.get_num_dofs(), ref_v_space->get_num_dofs());
    info("ndof: %d, err_est: %g%%", u_space.get_num_dofs() + v_space.get_num_dofs(), err_est);

    // If err_est too large, adapt the mesh.
    if (err_est < ERR_STOP || u_space.get_num_dofs() + v_space.get_num_dofs() >= NDOF_STOP) done = true;
    else {
      info("Adapting the coarse mesh.");
      done = hp.adapt(Tuple<RefinementSelectors::Selector *>(&selector, &selector), 
                      MULTI ? THRESHOLD_MULTI : THRESHOLD_SINGLE, STRATEGY, MESH_REGULARITY);
      if (u_space.get_num_dofs() + v_space.get_num_dofs() >= NDOF_STOP) done = true;
    }

    as++;
  }
  while (!done);

  int ndof = get_num_dofs(Tuple<Space *>(&u_space, &v_space));

#define ERROR_SUCCESS       0
#define ERROR_FAILURE      -1
  int ndof_allowed = 650;
  printf("ndof actual = %d\n", ndof);
  printf("ndof allowed = %d\n", ndof_allowed);
  if (ndof <= ndof_allowed) {      // ndofs was 626 at the time this test was created
    printf("Success!\n");
    return ERROR_SUCCESS;
  }
  else {
    printf("Failure!\n");
    return ERROR_FAILURE;
  }
}