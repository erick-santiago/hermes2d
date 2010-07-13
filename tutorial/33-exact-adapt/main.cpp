#define H2D_REPORT_WARN
#define H2D_REPORT_INFO
#define H2D_REPORT_VERBOSE
#define H2D_REPORT_FILE "application.log"
#include "hermes2d.h"

using namespace RefinementSelectors;

// This example shows how to adapt the mesh to match an arbitrary 
// given function. Note: This may be handy for the representation of 
// more complicated initial conditions in time-dependent problems
// (see example richards-adapt for illustration).
//
// The following parameters can be changed:

const int P_INIT = 2;                      // Initial polynomial degree of all mesh elements.
const double THRESHOLD = 0.2;              // This is a quantitative parameter of the adapt(...) function and
                                           // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 1;                    // Adaptive strategy:
                                           // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                           //   error is processed. If more elements have similar errors, refine
                                           //   all to keep the mesh symmetric.
                                           // STRATEGY = 1 ... refine all elements whose error is larger
                                           //   than THRESHOLD times maximum element error.
                                           // STRATEGY = 2 ... refine all elements whose error is larger
                                           //   than THRESHOLD.
                                           // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO;   // Predefined list of element refinement candidates. Possible values are
                                           // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO, H2D_HP_ANISO_H
                                           // H2D_HP_ANISO_P, H2D_HP_ANISO. See User Documentation for details.
const int MESH_REGULARITY = -1;   // Maximum allowed level of hanging nodes:
                                  // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                  // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                  // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                  // Note that regular meshes are not supported, this is due to
                                  // their notoriously bad performance.
const double ERR_STOP = 1.0;      // Stopping criterion for adaptivity (rel. error tolerance between the
const double CONV_EXP = 1.0;      // Default value is 1.0. This parameter influences the selection of
                                  // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
                                  // fine mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;      // Adaptivity process stops when the number of degrees of freedom grows
                                  // over this limit. This is to prevent h-adaptivity to go on forever.

// This function can be modified.
scalar f(double x, double y, double& dx, double& dy)
{
  dx = 0.25 * pow(x*x + y*y, -0.75) * 2 * x;
  dy = 0.25 * pow(x*x + y*y, -0.75) * 2 * y;
  return pow(x*x + y*y, 0.25);
}

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  mloader.load("square.mesh", &mesh);

  // Create an H1 space with default shapeset.
  H1Space space(&mesh, NULL, NULL, P_INIT);

  // Initialize refinement selector.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // Adapt mesh to represent initial condition with given accuracy.
  bool verbose = true; 
  Solution sln;
  adapt_to_exact_function_h1(&space, f, &selector, THRESHOLD, STRATEGY, 
                             MESH_REGULARITY, ERR_STOP, NDOF_STOP, verbose, &sln);   
  info("Final mesh: ndof = %d", space.get_num_dofs());

  // Wait for all views to be closed.
  View::wait();
  return 0;
}

