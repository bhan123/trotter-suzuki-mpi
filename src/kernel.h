/**
 * Distributed Trotter-Suzuki solver
 * Copyright (C) 2015 Luca Calderaro, 2012-2015 Peter Wittek,
 * 2010-2012 Carlos Bederián
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

#ifndef __KERNEL_H
#define __KERNEL_H
#include <string>
#include "trottersuzuki.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef CUDA
#include <cuda.h>
#include <cuda_runtime.h>
#endif

//These are for the MPI NEIGHBOURS
#define UP    0
#define DOWN  1
#define LEFT  2
#define RIGHT 3

#define BLOCK_WIDTH 128u
#define BLOCK_HEIGHT 128u

void process_band(bool two_wavefunctions, int offset_tile_x, int offset_tile_y, double alpha_x, double alpha_y, size_t tile_width, size_t block_width, size_t block_height, size_t halo_x, size_t read_y, size_t read_height, size_t write_offset, size_t write_height, 
                  double a, double b, double coupling_a, double coupling_b, const double *external_pot_real, const double *external_pot_imag, const double * p_real, const double * p_imag, 
                  const double * pb_real, const double * pb_imag, double * next_real, double * next_imag, int inner, int sides, bool imag_time);


/**
 * \brief This class define the prototipe of the kernel classes: CPU, GPU, Hybrid.
 */

class ITrotterKernel {
public:
    virtual ~ITrotterKernel() {};
    virtual void run_kernel() = 0;							///< Evolve the remaining blocks in the inner part of the tile.
    virtual void run_kernel_on_halo() = 0;					///< Evolve blocks of wave function at the edge of the tile. This comprises the halos.
    virtual void wait_for_completion() = 0;	                ///< Sincronize all the processes at the end of halos communication. Perform normalization for imaginary time evolution.
    virtual void get_sample(size_t dest_stride, size_t x, size_t y, size_t width, size_t height, double * dest_real, double * dest_imag, double * dest_real2=0, double * dest_imag2=0) const = 0;					///< Get the evolved wave function.
    virtual void normalization() = 0;
    virtual void rabi_coupling(double var, double delta_t) = 0;
    
    virtual bool runs_in_place() const = 0;
    virtual std::string get_name() const = 0;				///< Get kernel name.

    virtual void start_halo_exchange() = 0;					///< Exchange halos between processes.
    virtual void finish_halo_exchange() = 0;				///< Exchange halos between processes.

};

/**
 * \brief This class define the CPU kernel.
 *
 * This kernel provides real time and imaginary time evolution exploiting CPUs.
 * It implements a solver for a single wave function, whose evolution is governed by nonlinear Schrodinger equation. The Hamiltonian of the physical system includes:
 *  - static external potential
 *  - density self-interacting term
 *  - rotational energy
 */

class CPUBlock: public ITrotterKernel {
public:
    CPUBlock(Lattice *grid, State *state, Hamiltonian *hamiltonian, 
             double *_external_pot_real, double *_external_pot_imag, 
             double _a, double _b, double delta_t, 
             double _norm, bool _imag_time);
           
    
    CPUBlock(Lattice *grid, State *state1, State *state2, 
             Hamiltonian2Component *hamiltonian, 
             double **_external_pot_real, double **_external_pot_imag, 
             double *_a, double *_b, double delta_t,
             double *_norm, bool _imag_time);
    
    ~CPUBlock();
    void run_kernel_on_halo();          ///< Evolve blocks of wave function at the edge of the tile. This comprises the halos.
    void run_kernel();              ///< Evolve the remaining blocks in the inner part of the tile.
    void wait_for_completion();         ///< Sincronize all the processes at the end of halos communication. Perform normalization for imaginary time evolution.
    void get_sample(size_t dest_stride, size_t x, size_t y, size_t width, size_t height, double * dest_real, double * dest_imag, double * dest_real2=0, double * dest_imag2=0) const;  ///< Copy the wave function from the two buffers pointed by p_real and p_imag, without halos, to dest_real and dest_imag.
    void normalization();
    void rabi_coupling(double var, double delta_t);
  
    bool runs_in_place() const {
        return false;
    }
    /// Get kernel name.
    std::string get_name() const {
        return "CPU";
    };

    void start_halo_exchange();         ///< Start vertical halos exchange.
    void finish_halo_exchange();        ///< Start horizontal halos exchange.



private:
    double *p_real[2][2];       ///< Array of two pointers that point to two buffers used to store the real part of the wave function at i-th time step and (i+1)-th time step. 
    double *p_imag[2][2];       ///< Array of two pointers that point to two buffers used to store the imaginary part of the wave function at i-th time step and (i+1)-th time step.
    double *external_pot_real[2];   ///< Points to the matrix representation (real entries) of the operator given by the exponential of external potential.
    double *external_pot_imag[2];   ///< Points to the matrix representation (immaginary entries) of the operator given by the exponential of external potential.
    double *a;            ///< Diagonal value of the matrix representation of the operator given by the exponential of kinetic operator.
    double *b;            ///< Off diagonal value of the matrix representation of the operator given by the exponential of kinetic operator.
    double delta_x;         ///< Physical length between two neighbour along x axis dots of the lattice.
    double delta_y;         ///< Physical length between two neighbour along y axis dots of the lattice.
    double *norm;         ///< Squared norm of the wave function.
    double tot_norm;
    double *coupling_const;     ///< Coupling constant of the density self-interacting term.
    int sense;            ///< Takes values 0 or 1 and tells which of the two buffers pointed by p_real and p_imag is used to calculate the next time step.
    int state_index;
    size_t halo_x;          ///< Thickness of the vertical halos (number of lattice's dots).
    size_t halo_y;          ///< Thickness of the horizontal halos (number of lattice's dots).
    size_t tile_width;        ///< Width of the tile (number of lattice's dots).
    size_t tile_height;       ///< Height of the tile (number of lattice's dots).
    bool imag_time;         ///< True: imaginary time evolution; False: real time evolution.
    static const size_t block_width = BLOCK_WIDTH;      ///< Width of the lattice block which is cached (number of lattice's dots).
    static const size_t block_height = BLOCK_HEIGHT;    ///< Height of the lattice block which is cached (number of lattice's dots).
    bool two_wavefunctions;
    
    double alpha_x;         ///< Real coupling constant associated to the X*P_y operator, part of the angular momentum.
    double alpha_y;         ///< Real coupling constant associated to the Y*P_x operator, part of the angular momentum.
    int rot_coord_x;        ///< X axis coordinate of the center of rotation.
    int rot_coord_y;        ///< Y axis coordinate of the center of rotation.
    int start_x;          ///< X axis coordinate of the first dot of the processed tile.
    int start_y;          ///< Y axis coordinate of the first dot of the processed tile.
    int end_x;            ///< X axis coordinate of the last dot of the processed tile.
    int end_y;            ///< Y axis coordinate of the last dot of the processed tile.
    int inner_start_x;        ///< X axis coordinate of the first dot of the processed tile, which is not in the halo.
    int inner_start_y;        ///< Y axis coordinate of the first dot of the processed tile, which is not in the halo.
    int inner_end_x;        ///< X axis coordinate of the last dot of the processed tile, which is not in the halo.
    int inner_end_y;        ///< Y axis coordinate of the last dot of the processed tile, which is not in the halo.
    int *periods;         ///< Two dimensional array which takes entries 0 or 1. 1: periodic boundary condition along the corresponding axis; 0: closed boundary condition along the corresponding axis.
#ifdef HAVE_MPI
    MPI_Comm cartcomm;        ///< Ensemble of processes communicating the halos and evolving the tiles.
    int neighbors[4];       ///< Array that stores the processes' rank neighbour of the current process.
    MPI_Request req[8];       ///< Variable to manage MPI communication.
    MPI_Status statuses[8];     ///< Variable to manage MPI communication.
    MPI_Datatype horizontalBorder;  ///< Datatype for the horizontal halos.
    MPI_Datatype verticalBorder;  ///< Datatype for the vertical halos.
#endif
};

#ifdef CUDA

#define DISABLE_FMA

// NOTE: NEVER USE ODD NUMBERS FOR BLOCK DIMENSIONS
// thread block / shared memory block width
#define BLOCK_X 32
// shared memory block height
#define BLOCK_Y  (sizeof(double) == 8 ? 32 : 96)

#define STRIDE_Y 16

#define CUDA_SAFE_CALL(call) \
  if((call) != cudaSuccess) { \
    cudaError_t err = cudaGetLastError(); \
    fprintf (stderr, "CUDA error: %d\n", err ); \
    exit(-1);                                    \
  }

void setDevice(int commRank
#ifdef HAVE_MPI
               , MPI_Comm cartcomm
#endif
              );

void cc2kernel_wrapper(size_t tile_width, size_t tile_height, size_t offset_x, size_t offset_y, size_t halo_x, size_t halo_y, dim3 numBlocks, dim3 threadsPerBlock, cudaStream_t stream, double a, double b, const double * __restrict__ dev_external_pot_real, const double * __restrict__ dev_external_pot_imag, const double * __restrict__ pdev_real, const double * __restrict__ pdev_imag, double * __restrict__ pdev2_real, double * __restrict__ pdev2_imag, int inner, int horizontal, int vertical, bool imag_time);


/**
 * \brief This class define the GPU kernel.
 *
 * This kernel provides real time and imaginary time evolution exploiting GPUs.
 * It implements a solver for a single wave function, whose evolution is governed by linear Schrodinger equation. The Hamiltonian of the physical system includes:
 *  - static external potential
 */
 
class CC2Kernel: public ITrotterKernel {
public:
    CC2Kernel(Lattice *grid, State *state, double *_external_pot_real, double *_external_pot_imag, double a, double b, int halo_x, int halo_y, 
              double _norm, bool _imag_time
#ifdef HAVE_MPI
              , MPI_Comm cartcomm
#endif
             );
    ~CC2Kernel();
    void run_kernel_on_halo();				    ///< Evolve blocks of wave function at the edge of the tile. This comprises the halos.
    void run_kernel();							///< Evolve the remaining blocks in the inner part of the tile.
    void wait_for_completion();					///< Sincronize all the processes at the end of halos communication. Perform normalization for imaginary time evolution.
    void copy_results();						///< Copy wave function from buffer pointed by pdev_real and pdev_imag to buffers pointed by p_real and p_imag.
    void get_sample(size_t dest_stride, size_t x, size_t y, size_t width, size_t height, double * dest_real, double * dest_imag) const;  ///< Copy the wave function from the two buffers pointed by pdev_real and pdev_imag, without halos, to dest_real and dest_imag.
	void get_sample2(size_t dest_stride, size_t x, size_t y, size_t width, size_t height, double ** dest_real, double ** dest_imag) const {};
	void normalization() {};
	void rabi_coupling(double var, double delta_t) {};
	
    bool runs_in_place() const {
        return false;
    }
    /// Get kernel name.
    std::string get_name() const {
        return "CUDA";
    }

    void start_halo_exchange();		///< Empty function.
    void finish_halo_exchange();	///< Exchange halos.

private:
    dim3 numBlocks;						///< Number of blocks exploited in the lattice.
    dim3 threadsPerBlock;				///< Number of lattice dots in a block.
    cudaStream_t stream1;				///< Stream of sequential instructions performing evolution and communication on the halos blocks.
	cudaStream_t stream2;				///< Stream of sequential instructions performing evolution on the inner blocks.
	
    bool imag_time;						///< True: imaginary time evolution; False: real time evolution.
    double *p_real;						///< Point to  the real part of the wave function (stored in Host).
    double *p_imag;						///< Point to  the imaginary part of the wave function (stored in Host).
    double *external_pot_real;			///< Points to the matrix representation (real entries) of the operator given by the exponential of external potential (stored in Host).
    double *external_pot_imag;			///< Points to the matrix representation (imaginary entries) of the operator given by the exponential of external potential (stored in Host).
    double *dev_external_pot_real;		///< Points to the matrix representation (real entries) of the operator given by the exponential of external potential (stored in Device).
    double *dev_external_pot_imag;		///< Points to the matrix representation (imaginary entries) of the operator given by the exponential of external potential (stored in Device).
    double *pdev_real[2];				///< Array of two pointers that point to two buffers used to store the real part of the wave function at i-th time step and (i+1)-th time step (stored in Device).
    double *pdev_imag[2];				///< Array of two pointers that point to two buffers used to store the imaginary part of the wave function at i-th time step and (i+1)-th time step (stored in Device).
    double a;							///< Diagonal value of the matrix representation of the operator given by the exponential of kinetic operator.
    double b;							///< Off diagonal value of the matrix representation of the operator given by the exponential of kinetic operator.
    double delta_x;						///< Physical length between two neighbour along x axis dots of the lattice.
    double delta_y;						///< Physical length between two neighbour along y axis dots of the lattice.
    double norm;						///< Squared norm of the wave function.
    int sense;							///< Takes values 0 or 1 and tells which of the two buffers pointed by p_real and p_imag is used to calculate the next time step.
    size_t halo_x;						///< Thickness of the vertical halos (number of lattice's dots).
    size_t halo_y;						///< Thickness of the horizontal halos (number of lattice's dots).
    size_t tile_width;					///< Width of the tile (number of lattice's dots).
    size_t tile_height;					///< Height of the tile (number of lattice's dots).
	int *periods;						///< Two dimensional array which takes entries 0 or 1. 1: periodic boundary condition along the corresponding axis; 0: closed boundary condition along the corresponding axis.
#ifdef HAVE_MPI
    MPI_Comm cartcomm;
#endif
    int neighbors[4];					///< Array that stores the processes' rank neighbour of the current process.
    int start_x;						///< X axis coordinate of the first dot of the processed tile.
    int start_y;						///< Y axis coordinate of the first dot of the processed tile.
    int end_x;							///< X axis coordinate of the last dot of the processed tile.
    int end_y;							///< Y axis coordinate of the last dot of the processed tile.
    int inner_start_x;					///< X axis coordinate of the first dot of the processed tile, which is not in the halo.
    int inner_start_y;					///< Y axis coordinate of the first dot of the processed tile, which is not in the halo.
    int inner_end_x;					///< X axis coordinate of the last dot of the processed tile, which is not in the halo.
    int inner_end_y;					///< Y axis coordinate of the last dot of the processed tile, which is not in the halo.
    double *left_real_receive;			///< Point to the buffer used to receive the real left vertical halo from the corresponding neighbour.
    double *left_real_send;				///< Point to the buffer used to send the real left vertical halo to the corresponding neighbour.
    double *right_real_receive;			///< Point to the buffer used to receive the real right vertical halo from the corresponding neighbour.
    double *right_real_send;			///< Point to the buffer used to send the real right vertical halo to the corresponding neighbour.
    double *left_imag_receive;			///< Point to the buffer used to receive the imaginary left vertical halo from the corresponding neighbour.
    double *left_imag_send;				///< Point to the buffer used to send the imaginary left vertical halo to the corresponding neighbour.
    double *right_imag_receive;			///< Point to the buffer used to receive the imaginary right vertical halo from the corresponding neighbour.
    double *right_imag_send;			///< Point to the buffer used to send the imaginary right vertical halo to the corresponding neighbour.
    double *bottom_real_receive;		///< Point to the buffer used to receive the real bottom horizontal halo from the corresponding neighbour.
    double *bottom_real_send;			///< Point to the buffer used to send the real bottom horizontal halo to the corresponding neighbour.
    double *top_real_receive;			///< Point to the buffer used to receive the real top horizontal halo from the corresponding neighbour.
    double *top_real_send;				///< Point to the buffer used to send the real top horizontal halo to the corresponding neighbour.
    double *bottom_imag_receive;		///< Point to the buffer used to receive the imaginary bottom horizontal halo from the corresponding neighbour.
    double *bottom_imag_send;			///< Point to the buffer used to send the imaginary bottom horizontal halo to the corresponding neighbour.
    double *top_imag_receive;			///< Point to the buffer used to receive the real top horizontal halo from the corresponding neighbour.
    double *top_imag_send;				///< Point to the buffer used to send the real bottom horizontal halo to the corresponding neighbour.

};

/**
 * \brief This class define the Hybrid kernel.
 *
 * This kernel provides real time and imaginary time evolution exploiting GPUs and CPUs.
 * It implements a solver for a single wave function, whose evolution is governed by linear Schrodinger equation. The Hamiltonian of the physical system includes:
 *  - static external potential
 */
 
class HybridKernel: public ITrotterKernel {
public:
    HybridKernel(double *p_real, double *p_imag, double *_external_pot_real, double *_external_pot_imag, double a, double b, double _coupling_const, double _delta_x, double _delta_y,
                 int matrix_width, int matrix_height, int halo_x, int halo_y, int * _periods, double _norm, bool _imag_time
#ifdef HAVE_MPI
                 , MPI_Comm cartcomm
#endif
                );
    ~HybridKernel();
    void run_kernel_on_halo();					///< Evolve blocks of wave function at the edge of the tile. This comprises the halos.
    void run_kernel();							///< Evolve the remaining blocks in the inner part of the tile.
    void wait_for_completion();					///< Sincronize all the processes at the end of halos communication. Perform normalization for imaginary time evolution.
    void get_sample(size_t dest_stride, size_t x, size_t y, size_t width, size_t height, double * dest_real, double * dest_imag) const;		///< Copy the wave function from the buffers pointed by p_real, p_imag, pdev_real and pdev_imag, without halos, to dest_real and dest_imag.
    void get_sample2(size_t dest_stride, size_t x, size_t y, size_t width, size_t height, double ** dest_real, double ** dest_imag) const {};
    void normalization() {};
    void rabi_coupling(double var, double delta_t) {};
    
    bool runs_in_place() const {
        return false;
    }
    /// Get kernel name.
    std::string get_name() const {
        std::stringstream name;
        name << "Hybrid";
#ifdef _OPENMP
        name << "-OpenMP-" << omp_get_max_threads();
#endif
        return name.str();
    };

    void start_halo_exchange();				///< Start vertical halos exchange (between Hosts).
    void finish_halo_exchange();			///< Start horizontal halos exchange (between Hosts).

private:
    dim3 numBlocks;							///< Number of blocks exploited in the lattice.
    dim3 threadsPerBlock;					///< Number of lattice dots in a block.
    cudaStream_t stream;					///< Stream of sequential instructions performing evolution and communication on the Device lattice part.

    bool imag_time;							///< True: imaginary time evolution; False: real time evolution.
    double *p_real[2];						///< Array of two pointers that point to two buffers used to store the real part of the wave function at i-th time step and (i+1)-th time step (Host part). 
    double *p_imag[2];						///< Array of two pointers that point to two buffers used to store the imaginary part of the wave function at i-th time step and (i+1)-th time step (Host part).
    double *pdev_real[2];					///< Array of two pointers that point to two buffers used to store the real part of the wave function at i-th time step and (i+1)-th time step (Device part).
    double *pdev_imag[2];					///< Array of two pointers that point to two buffers used to store the imaginary part of the wave function at i-th time step and (i+1)-th time step (Device part).
    double *external_pot_real;				///< Points to the matrix representation (real entries) of the operator given by the exponential of external potential (Host part).
    double *external_pot_imag;				///< Points to the matrix representation (immaginary entries) of the operator given by the exponential of external potential (Host part).
    double *dev_external_pot_real;			///< Points to the matrix representation (real entries) of the operator given by the exponential of external potential (Device part).
    double *dev_external_pot_imag;			///< Points to the matrix representation (imaginary entries) of the operator given by the exponential of external potential (Device part).
    double a;								///< Diagonal value of the matrix representation of the operator given by the exponential of kinetic operator.
    double b;								///< Off diagonal value of the matrix representation of the operator given by the exponential of kinetic operator.
    double coupling_const;    				///< Coupling constant of the density self-interacting term (not implemented in this kernel).
    double delta_x;							///< Physical length between two neighbour along x axis dots of the lattice.
    double delta_y;							///< Physical length between two neighbour along y axis dots of the lattice.
    double norm;							///< Squared norm of the wave function.
    int sense;								///< Takes values 0 or 1 and tells which of the two buffers pointed by p_real and p_imag is used to calculate the next time step.
    size_t halo_x;							///< Thickness of the vertical halos (number of lattice's dots).
    size_t halo_y;							///< Thickness of the horizontal halos (number of lattice's dots).
    size_t tile_width;						///< Width of the tile (number of lattice's dots).
    size_t tile_height;						///< Height of the tile (number of lattice's dots).
    static const size_t block_width = BLOCK_WIDTH;		///< Width of the lattice block which is cached (number of lattice's dots).
    static const size_t block_height = BLOCK_HEIGHT;	///< Height of the lattice block which is cached (number of lattice's dots).
    size_t gpu_tile_width;					///< Tile width processes by Device.
    size_t gpu_tile_height;					///< Tile height processes by Device.
    size_t gpu_start_x;						///< X axis coordinate of the first dot of the processed tile in Device.
    size_t gpu_start_y;						///< Y axis coordinate of the first dot of the processed tile in Device.
    size_t n_bands_on_cpu;					///< Number of blocks in a column in Device's tile.

    int neighbors[4];						///< Array that stores the processes' rank neighbour of the current process.
    int start_x;							///< X axis coordinate of the first dot of the processed tile.
    int start_y;							///< Y axis coordinate of the first dot of the processed tile.
    int end_x;								///< X axis coordinate of the last dot of the processed tile.
    int end_y;								///< Y axis coordinate of the last dot of the processed tile.
    int inner_start_x;						///< X axis coordinate of the first dot of the processed tile, which is not in the halo.
    int inner_start_y;						///< Y axis coordinate of the first dot of the processed tile, which is not in the halo.
    int inner_end_x;						///< X axis coordinate of the last dot of the processed tile, which is not in the halo.
    int inner_end_y;						///< Y axis coordinate of the last dot of the processed tile, which is not in the halo.
    int *periods;							///< Two dimensional array which takes entries 0 or 1. 1: periodic boundary condition along the corresponding axis; 0: closed boundary condition along the corresponding axis.
#ifdef HAVE_MPI
    MPI_Comm cartcomm;						///< Ensemble of processes communicating the halos and evolving the tiles.
    MPI_Request req[8];						///< Variable to manage MPI communication (between Hosts).
    MPI_Status statuses[8];					///< Variable to manage MPI communication (between Hosts).
    MPI_Datatype horizontalBorder;			///< Datatype for the horizontal halos (Host halos).
    MPI_Datatype verticalBorder;			///< Datatype for the vertical halos (Host halos).
#endif
};

#endif // CUDA

#endif // __KERNEL_H
