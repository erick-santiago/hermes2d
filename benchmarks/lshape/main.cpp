#define H2D_REPORT_WARN
#define H2D_REPORT_INFO
#define H2D_REPORT_VERBOSE
#define H2D_REPORT_FILE "application.log"
#include "hermes2d.h"
#include "solver_umfpack.h"

using namespace RefinementSelectors;

//  This is a standard benchmark for adaptive FEM algorithms. The exact solution is a harmonic 
//  function in an L-shaped domain and it contains singular gradient at the re-entrant corner.
//
//  PDE: -Laplace u = 0.
//
//  Known exact solution, see functions fn() and fndd().
//
//  Domain: L-shape domain, see the file lshape.mesh.
//
//  BC:  Dirichlet, given by exact solution.
//
//  The following parameters can be changed:

const int INIT_REF_NUM = 1;       // Number of initial mesh refinements.
const int P_INIT = 4;             // Initial polynomial degree of all mesh elements.
const double THRESHOLD = 0.3;     // This is a quantitative parameter of the adapt(...) function and
                                  // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 0;           // Adaptive strategy:
                                  // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                  //   error is processed. If more elements have similar errors, refine
                                  //   all to keep the mesh symmetric.
                                  // STRATEGY = 1 ... refine all elements whose error is larger
                                  //   than THRESHOLD times maximum element error.
                                  // STRATEGY = 2 ... refine all elements whose error is larger
                                  //   than THRESHOLD.
                                  // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO_H; // Predefined list of element refinement candidates. Possible values are
                                           // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                           // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                           // See User Documentation for details.
const int MESH_REGULARITY = -1;   // Maximum allowed level of hanging nodes:
                                  // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                  // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                  // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                  // Note that regular meshes are not supported, this is due to
                                  // their notoriously bad performance.
const double CONV_EXP = 1.0;      // Default value is 1.0. This parameter influences the selection of
                                  // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
                                  // error behavior err \approx const1*exp(-const2*pow(NDOF, CONV_EXP)).
const double ERR_STOP = 0.01;     // Stopping criterion for adaptivity (rel. error tolerance between the
                                  // fine mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;      // Adaptivity process stops when the number of degrees of freedom grows
                                  // over this limit. This is to prevent h-adaptivity to go on forever.

// Exact solution.
#include "exact_solution.cpp"

// Boundary condition types.
BCType bc_types(int marker)
{
  return BC_ESSENTIAL;
}

// Essential (Dirichlet) boundary condition values.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return fn(x, y);
}

// Weak forms.
#include "forms.cpp"

int main(int argc, char* argv[])
{
  // Time measurement
  TimePeriod cpu_time;
  cpu_time.tick();

  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  mloader.load("lshape.mesh", &mesh);

  // Perform initial mesh refinement.
  mesh.refine_all_elements();

  // Initialize the shapeset and the cache.
  H1Shapeset shapeset;
  PrecalcShapeset pss(&shapeset);

  // Create an H1 space.
  H1Space space(&mesh, &shapeset);
  space.set_bc_types(bc_types);
  space.set_essential_bc_values(essential_bc_values);
  space.set_uniform_order(P_INIT);

  // Enumerate degrees of freedom.
  int ndof = assign_dofs(&space);

  // Initialize the weak formulation.
  WeakForm wf;
  wf.add_biform(callback(bilinear_form), H2D_SYM);

  // Initialize views.
  ScalarView sview("Coarse solution", 0, 0, 500, 400);
  OrderView  oview("Polynomial orders", 505, 0, 500, 400);

  // Matrix solver.
  UmfpackSolver solver;

  // Initialize refinement selector.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER, &shapeset);

  // DOF and CPU convergence graphs.
  SimpleGraph graph_dof_est, graph_dof_exact, graph_cpu_est, graph_cpu_exact;

  // Adaptivity loop:
  int as = 1; bool done = false;
  Solution sln_coarse, sln_fine;
  do
  {
    info("---- Adaptivity step %d:", as);

    // Solve the coarse mesh problem.
    LinSystem ls(&wf, &solver);
    ls.set_space(&space);
    ls.set_pss(&pss);
    ls.assemble();
    ls.solve(&sln_coarse);

    // Time measurement.
    cpu_time.tick();

    // Calculate error wrt. exact solution.
    ExactSolution exact(&mesh, fndd);
    double err_exact = h1_error(&sln_coarse, &exact) * 100;

    // View the solution and mesh.
    sview.show(&sln_coarse);
    oview.show(&space);

    // Skip exact error calculation and visualization time. 
    cpu_time.tick(H2D_SKIP);

    // Solve the fine mesh problem.
    RefSystem rs(&ls);
    rs.assemble();
    rs.solve(&sln_fine);

    // Calculate error estimate wrt. fine mesh solution.
    H1Adapt hp(&space);
    hp.set_solutions(&sln_coarse, &sln_fine);
    double err_est = hp.calc_error() * 100;

    // Report results.  
    info("ndof_coarse: %d, ndof_fine: %d, err_est: %g%%, err_exact: %g%%", 
         space.get_num_dofs(), rs.get_space(0)->get_num_dofs(), err_est, err_exact);

    // Add entries to DOF convergence graphs.
    graph_dof_exact.add_values(space.get_num_dofs(), err_exact);
    graph_dof_exact.save("conv_dof_exact.dat");
    graph_dof_est.add_values(space.get_num_dofs(), err_est);
    graph_dof_est.save("conv_dof_est.dat");

    // Add entries to CPU convergence graphs.
    graph_cpu_exact.add_values(cpu_time.accumulated(), err_exact);
    graph_cpu_exact.save("conv_cpu_exact.dat");
    graph_cpu_est.add_values(cpu_time.accumulated(), err_est);
    graph_cpu_est.save("conv_cpu_est.dat");

    // If err_est too large, adapt the mesh.
    if (err_est < ERR_STOP) done = true;
    else {
      hp.adapt(&selector, THRESHOLD, STRATEGY, MESH_REGULARITY);
      ndof = assign_dofs(&space);
      if (ndof >= NDOF_STOP) done = true;
    }

    as++;
  }
  while (done == false);
  verbose("Total running time: %g s", cpu_time.accumulated());

  // Show the fine mesh solution - the final result.
  sview.set_title("Final solution");
  sview.show(&sln_fine);

  // Wait for all views to be closed.
  View::wait();
  return 0;
}
