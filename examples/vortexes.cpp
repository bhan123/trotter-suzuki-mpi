/**
 * Massively Parallel Trotter-Suzuki Solver
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#ifdef HAVE_MPI
#include <mpi.h>
#endif
#include "trottersuzuki.h"

#define LENGTH 25
#define DIM 200
#define ITERATIONS 100
#define PARTICLES_NUM 1.e+6
#define KERNEL_TYPE "cpu"
#define SNAPSHOTS 60
#define SNAP_PER_STAMP 3
#define COUPLING_CONST_2D 7.116007999594e-4

int main(int argc, char** argv) {
    double angular_velocity = 0.9;
    const double particle_mass = 1.;
    bool imag_time = true;
    double delta_t = 2.e-4;
    double length_x = double(LENGTH), length_y = double(LENGTH);
    double coupling_const = double(COUPLING_CONST_2D);
#ifdef HAVE_MPI
    MPI_Init(&argc, &argv);
#endif

    //set lattice
    Lattice *grid = new Lattice(DIM, length_x, length_y, false, false, angular_velocity);
    //set initial state
    State *state = new GaussianState(grid, 0.2, 0., 0., PARTICLES_NUM);
    //set hamiltonian
    Potential *potential = new HarmonicPotential(grid, 1., 1.);
    Hamiltonian *hamiltonian = new Hamiltonian(grid, potential, particle_mass, coupling_const, angular_velocity);
    //set evolution
    Solver *solver = new Solver(grid, state, hamiltonian, delta_t, KERNEL_TYPE);

    //set file output directory
    stringstream fileprefix;
    string dirname = "vortexesdir";
    mkdir(dirname.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    fileprefix << dirname << "/file_info.txt";
    ofstream out(fileprefix.str().c_str());

    double norm2 = state->get_squared_norm();
    double rot_energy = solver->get_rotational_energy();
    double tot_energy = solver->get_total_energy();
    double kin_energy = solver->get_kinetic_energy();

    if(grid->mpi_rank == 0){
      out << "iterations \t rotation energy \t kin energy \t total energy \t norm2\n";
      out << "0\t" << rot_energy << "\t" << kin_energy << "\t" << tot_energy << "\t" << norm2 << endl;
    }

    fileprefix.str("");
    fileprefix << dirname << "/" << 0;
    state->write_particle_density(fileprefix.str());

    for(int count_snap = 0; count_snap < SNAPSHOTS; count_snap++) {
        solver->evolve(ITERATIONS, imag_time);

        norm2 = state->get_squared_norm();
        rot_energy = solver->get_rotational_energy();
        tot_energy = solver->get_total_energy();
        kin_energy = solver->get_kinetic_energy();
        if (grid->mpi_rank == 0){
            out << (count_snap + 1) * ITERATIONS << "\t" << rot_energy << "\t" << kin_energy << "\t" << tot_energy << "\t" << norm2 << endl;
        }

        //stamp phase and particles density
        if(count_snap % SNAP_PER_STAMP == 0.) {
            //get and stamp phase
            fileprefix.str("");
            fileprefix << dirname << "/" << ITERATIONS * (count_snap + 1);
            state->write_phase(fileprefix.str());
            //get and stamp particles density
            state->write_particle_density(fileprefix.str());
        }
    }
    out.close();
    fileprefix.str("");
    fileprefix << dirname << "/" << 1 << "-" << ITERATIONS * SNAPSHOTS;
    state->write_to_file(fileprefix.str());
    cout << "\n";
    delete solver;
    delete hamiltonian;
    delete potential;
    delete state;
    delete grid;
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    return 0;
}
