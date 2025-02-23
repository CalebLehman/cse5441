#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mpi.h>
#include <omp.h>

#include "amr.h"
#include "common.h"

typedef enum tag {
    ar_tag,
    ep_tag,
    n_tag,
    perimeter_tag,
    num_nhbrs_tag,
    offsets_tag,
    self_overlaps_tag,
    total_nhbrs_tag,
    nhbr_id_tag,
    overlap_tag,
    dsv_tag,
    pos_tag,
    run_tag
} tag;

const char* usage = "\
Usage: amr [affect-rate] [epsilon] [test-file]\n\
\n\
affect-rate: float value controlling the effect of neighboring boxes\n\
epislon    : float value determining the cutoff for convergence\n\
test-file  : test file with input to AMR problem\n";

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (rank == 0) {
        /**
         * Parse command-line arguments
         */
        if (argc != 4) {
            printf("%s", usage);
            exit(1);
        }
        float affect_rate = strtof(argv[1], NULL);
        float epsilon     = strtof(argv[2], NULL);
        char* test_file   = argv[3];

        /**
         * Parse input data from standard input
         */
        AMRInput* input = parseInput(test_file);

        /**
         * Run and collect timing information
         */
        time_t  time_before;
        time(&time_before);
        clock_t clock_before = clock();
        struct timespec gettime_before;
        clock_gettime(CLOCK_REALTIME, &gettime_before);

        AMROutput output = run_master(input, affect_rate, epsilon);

        time_t time_after;
        time(&time_after);
        time_t clock_after = clock();
        struct timespec gettime_after;
        clock_gettime(CLOCK_REALTIME, &gettime_after);

        output.time_seconds  = difftime(time_after, time_before);
        output.clock_seconds = (clock_after - clock_before) / (double) CLOCKS_PER_SEC;
        output.gettime_seconds = (double) (
            (gettime_after.tv_sec - gettime_before.tv_sec) +
            ((gettime_after.tv_nsec - gettime_before.tv_nsec) / 1000000000.0)
        );

        /**
         * Display results
         */
        displayOutput(output);

        /**
         * Clean up
         */
        destroyInput(input);
    } else {
        /**
         * Non-master processes just wait for master to send work
         */
        run_computation();
    }

    MPI_Finalize();
    return 0;
}

/**
 * {@inheritDoc}
 */
AMROutput run_master(AMRInput* input, float affect_rate, float epsilon) {
    /**
     * Repeat until convergence
     */
    AMRMaxMin max_min = getMaxMin(input);

    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    Count* starts = malloc((size - 1) * sizeof(*starts));
    Count* ends   = malloc((size - 1) * sizeof(*ends));
    for (int rank = 1; rank < size; ++rank) {
        MPI_Send(&affect_rate, 1, MPI_FLOAT, rank, ar_tag, MPI_COMM_WORLD);
        MPI_Send(&epsilon, 1, MPI_FLOAT, rank, ep_tag, MPI_COMM_WORLD);
        MPI_Send(&input->N, 1, COUNT_MPI_TYPE, rank, n_tag, MPI_COMM_WORLD);

        starts[rank - 1] = (rank-1) * (input->N / (size - 1));
        ends  [rank - 1] = (rank == size - 1)
            ? input->N
            : rank * (input->N / (size - 1));

        Count pos[2] = { starts[rank - 1], ends[rank - 1] };
        MPI_Send(&pos[0], 2, COUNT_MPI_TYPE, rank, pos_tag, MPI_COMM_WORLD);

        MPI_Send(&input->perimeters[0], input->N, COORD_MPI_TYPE, rank, perimeter_tag, MPI_COMM_WORLD);
        MPI_Send(&input->num_nhbrs[0], input->N, COUNT_MPI_TYPE, rank, num_nhbrs_tag, MPI_COMM_WORLD);
        MPI_Send(&input->offsets[0], input->N, COUNT_MPI_TYPE, rank, offsets_tag, MPI_COMM_WORLD);
        MPI_Send(&input->self_overlaps[0], input->N, COORD_MPI_TYPE, rank, self_overlaps_tag, MPI_COMM_WORLD);

        MPI_Send(&input->total_nhbrs, 1, COUNT_MPI_TYPE, rank, total_nhbrs_tag, MPI_COMM_WORLD);
        MPI_Send(&input->nhbr_ids[0], input->total_nhbrs, COUNT_MPI_TYPE, rank, nhbr_id_tag, MPI_COMM_WORLD);
        MPI_Send(&input->overlaps[0], input->total_nhbrs, COORD_MPI_TYPE, rank, overlap_tag, MPI_COMM_WORLD);
    }

    unsigned long iter;
    for (iter = 0; (max_min.max - max_min.min) / max_min.max > epsilon; ++iter, max_min = getMaxMin(input)) {
        /**
         * Send vals to processes
         */
        int running = 1;
        for (int rank = 1; rank < size; ++rank) {
            MPI_Send(&running, 1, MPI_INT, rank, run_tag, MPI_COMM_WORLD);
            MPI_Send(&input->vals[0], input->N, DSV_MPI_TYPE, rank, dsv_tag, MPI_COMM_WORLD);
        }

        /**
         * Receive vals from processes
         */
        for (int rank = 1; rank < size; ++rank) {
            MPI_Status status;
            Count start = starts[rank - 1];
            Count end   = ends  [rank - 1];
            MPI_Recv(&input->vals[start], end - start, DSV_MPI_TYPE, rank, dsv_tag, MPI_COMM_WORLD, &status);
        }
    }
    int running = 0;
    for (int rank = 1; rank < size; ++rank) {
        MPI_Send(&running, 1, MPI_INT, rank, run_tag, MPI_COMM_WORLD);
    }

    free(starts);
    free(ends);

    AMROutput result;
    result.affect_rate = affect_rate;
    result.epsilon     = epsilon;
    result.iterations  = iter;
    result.max         = max_min.max;
    result.min         = max_min.min;
    return result;
}

void run_computation() {
    MPI_Status status;

    float affect_rate;
    MPI_Recv(&affect_rate, 1, MPI_FLOAT, 0, ar_tag, MPI_COMM_WORLD, &status);

    float epsilon;
    MPI_Recv(&epsilon, 1, MPI_FLOAT, 0, ep_tag, MPI_COMM_WORLD, &status);

    Count N;
    MPI_Recv(&N, 1, COUNT_MPI_TYPE, 0, n_tag, MPI_COMM_WORLD, &status);
    DSV* vals         = malloc(N * sizeof(*vals));
    DSV* updated_vals = malloc(N * sizeof(*updated_vals));
    Count pos[2];
    MPI_Recv(&pos[0], 2, COUNT_MPI_TYPE, 0, pos_tag, MPI_COMM_WORLD, &status);
    Count start = pos[0];
    Count end   = pos[1];

    Coord* perimeters    = malloc(N * sizeof(*perimeters));
    Count* num_nhbrs     = malloc(N * sizeof(*num_nhbrs));
    Count* offsets       = malloc(N * sizeof(*offsets));
    Count* self_overlaps = malloc(N * sizeof(*self_overlaps));
    MPI_Recv(&perimeters[0], N, COORD_MPI_TYPE, 0, perimeter_tag, MPI_COMM_WORLD, &status);
    MPI_Recv(&num_nhbrs[0], N, COUNT_MPI_TYPE, 0, num_nhbrs_tag, MPI_COMM_WORLD, &status);
    MPI_Recv(&offsets[0], N, COUNT_MPI_TYPE, 0, offsets_tag, MPI_COMM_WORLD, &status);
    MPI_Recv(&self_overlaps[0], N, COORD_MPI_TYPE, 0, self_overlaps_tag, MPI_COMM_WORLD, &status);

    Count total_nhbrs;
    MPI_Recv(&total_nhbrs, 1, COUNT_MPI_TYPE, 0, total_nhbrs_tag, MPI_COMM_WORLD, &status);
    Count* nhbr_ids = malloc(total_nhbrs * sizeof(*nhbr_ids));
    Coord* overlaps = malloc(total_nhbrs * sizeof(*overlaps));
    MPI_Recv(&nhbr_ids[0], total_nhbrs, COUNT_MPI_TYPE, 0, nhbr_id_tag, MPI_COMM_WORLD, &status);
    MPI_Recv(&overlaps[0], total_nhbrs, COORD_MPI_TYPE, 0, overlap_tag, MPI_COMM_WORLD, &status);

    int running;
    MPI_Recv(&running, 1, MPI_INT, 0, run_tag, MPI_COMM_WORLD, &status);
    while (running == 1) {
        /**
         * Receive current vals
         */
        MPI_Recv(&vals[0], N, DSV_MPI_TYPE, 0, dsv_tag, MPI_COMM_WORLD, &status);

        /**
         * Compute updated vals
         */
        #pragma omp parallel
        {
            #pragma omp for schedule(static)
            for (int i = start; i < end; ++i) {
                /**
                 * Compute updated DSV
                 */
                updated_vals[i] = self_overlaps[i] * vals[i];
                for (int nhbr = 0; nhbr < num_nhbrs[i]; ++nhbr) {
                    updated_vals[i] += overlaps[offsets[i] + nhbr] * vals[nhbr_ids[offsets[i] + nhbr]];
                }
                updated_vals[i] /= perimeters[i];
                updated_vals[i] = vals[i] * (1 - affect_rate)
                    + updated_vals[i] * affect_rate;
            }
        }

        /**
         * Send back
         */
        MPI_Send(&updated_vals[start], end - start, DSV_MPI_TYPE, 0, dsv_tag, MPI_COMM_WORLD);

        MPI_Recv(&running, 1, MPI_INT, 0, run_tag, MPI_COMM_WORLD, &status);
    }

    return;
}
